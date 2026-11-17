/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef TRINITYCORE_BOUNDING_INTERVAL_HIERARCHY_WRAPPER_H
#define TRINITYCORE_BOUNDING_INTERVAL_HIERARCHY_WRAPPER_H

#include "BoundingIntervalHierarchy.h"
#include "Log.h"
#include <cmath>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <vector>

template<class T, class BoundsFunc = BoundsTrait<T> >
class BIHWrap
{
    template<class RayCallback>
    struct MDLCallback
    {
        std::size_t objects_size;
        T const* const* objects;
        RayCallback& _callback;

        MDLCallback(RayCallback& callback, T const* const* objects_array, std::size_t objects_size ) : objects_size(objects_size), objects(objects_array), _callback(callback) { }

        /// Intersect ray
        bool operator() (G3D::Ray const& ray, std::size_t idx, float& maxDist, bool /*stopAtFirst*/)
        {
            if (idx >= objects_size)
                return false;
            if (T const* obj = objects[idx])
                return _callback(ray, *obj, maxDist/*, stopAtFirst*/);
            return false;
        }

        /// Intersect point
        void operator() (G3D::Vector3 const& p, std::size_t idx)
        {
            if (idx >= objects_size)
                return;
            if (T const* obj = objects[idx])
                _callback(p, *obj);
        }
    };

    BIH m_tree;
    std::vector<T const*> m_objects;
    std::unordered_map<T const*, std::size_t> m_obj2Idx;
    int unbalanced_times;

    // Bounds the tree can place: all six values finite, and low not above high on any axis (an empty box, low > high, cannot be placed).
    static bool UsableBounds(G3D::AABox const& box)
    {
        for (int axis = 0; axis < 3; ++axis)
            if (!std::isfinite(box.low()[axis]) || !std::isfinite(box.high()[axis]) || box.low()[axis] > box.high()[axis])
                return false;
        return true;
    }

public:
    BIHWrap() : unbalanced_times(0) { }

    void insert(T const& obj)
    {
        auto [itr, isNew] = m_obj2Idx.try_emplace(&obj, m_objects.size());
        if (!isNew)
            return;

        m_objects.push_back(itr->first);
        ++unbalanced_times;
    }

    void remove(T const& obj)
    {
        auto node = m_obj2Idx.extract(&obj);
        if (!node)
            return;

        if (node.key() != m_objects.back())
        {
            // update index of last element (will be swapped with removed one)
            m_obj2Idx.find(m_objects.back())->second = node.mapped();

            // move last into removed element slot
            m_objects[node.mapped()] = m_objects.back();
        }

        m_objects.pop_back();

        ++unbalanced_times;
    }

    // Removes the objects whose bounds the tree cannot place, logging each one. Run before every build: an infinite bound makes
    // BIH::build loop for ever (the split never narrows), and NaN or low-above-high bounds can make it throw "invalid node overlap".
    void dropUnusableBounds(char const* when)
    {
        std::vector<T const*> unusable;
        for (T const* obj : m_objects)
        {
            G3D::AABox box;
            BoundsFunc()(obj, box);
            if (!UsableBounds(box))
                unusable.push_back(obj);
        }
        for (T const* obj : unusable)
        {
            G3D::AABox box;
            BoundsFunc()(obj, box);
            TC_LOG_ERROR("maps", "BIHWrap: {}: dropped object {} with bounds lo=({}, {}, {}) hi=({}, {}, {})", when, static_cast<void const*>(obj),
                box.low().x, box.low().y, box.low().z, box.high().x, box.high().y, box.high().z);
            remove(*obj);
        }
    }

    void balance()
    {
        if (unbalanced_times == 0)
            return;

        // Objects with unusable bounds stay out of the tree (no dynamic collision for them) until they are removed and inserted
        // again; the build never sees them, so it can neither hang nor throw on them.
        dropUnusableBounds("before the build");

        try
        {
            m_tree.build(m_objects, BoundsFunc());
        }
        catch (std::logic_error const& error)
        {
            // Backstop: BIH::build threw for usable bounds too. Nothing on the map update thread catches it, so the world server would
            // abort. The tree is left empty (no dynamic collision on this map) until it changes, and the first objects are logged.
            TC_LOG_ERROR("maps", "BIHWrap: building the tree of {} object(s) failed ({}); the tree is left empty", m_objects.size(), error.what());
            std::size_t logged = 0;
            for (T const* obj : m_objects)
            {
                if (logged++ >= 8)
                    break;
                G3D::AABox box;
                BoundsFunc()(obj, box);
                TC_LOG_ERROR("maps", "BIHWrap:   object {} bounds lo=({}, {}, {}) hi=({}, {}, {})", static_cast<void const*>(obj),
                    box.low().x, box.low().y, box.low().z, box.high().x, box.high().y, box.high().z);
            }
            m_tree.build(std::vector<T const*>(), BoundsFunc());
        }
        unbalanced_times = 0;
    }

    template<typename RayCallback>
    void intersectRay(G3D::Ray const& ray, RayCallback& intersectCallback, float& maxDist)
    {
        balance();
        MDLCallback<RayCallback> temp_cb(intersectCallback, m_objects.data(), m_objects.size());
        m_tree.intersectRay(ray, temp_cb, maxDist, true);
    }

    template<typename IsectCallback>
    void intersectPoint(G3D::Vector3 const& point, IsectCallback& intersectCallback)
    {
        balance();
        MDLCallback<IsectCallback> callback(intersectCallback, m_objects.data(), m_objects.size());
        m_tree.intersectPoint(point, callback);
    }

    std::span<T const* const> getObjects() const { return m_objects; }
};

#endif // TRINITYCORE_BOUNDING_INTERVAL_HIERARCHY_WRAPPER_H
