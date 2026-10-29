/*
 * Interop test: ModuleWire (core, proto2 hand-written codec) against forever-module-sdk's generated
 * protobuf code. Messages are checked in both directions, so a field-number or type mismatch on
 * either side fails here.
 *
 * Not part of the worldserver build. Build and run it against an SDK build tree, for example:
 *   g++ -std=c++17 -Isrc/common -Isrc/server/game/Modules -Isrc/server/game/Bots \
 *       -I<sdk>/include -I<sdk-build>/generated/forever_module \
 *       src/server/game/Modules/ModuleWire.cpp \
 *       src/server/game/Modules/tests/ModuleWireInterop.cpp \
 *       <sdk-build>/generated/forever_module/{handshake,events,control,envelope}.pb.cc \
 *       -lprotobuf -o module_wire_interop && ./module_wire_interop
 */

#include "BotTalentSpec.h"
#include "ModuleWire.h"

#include "envelope.pb.h"
#include "actions.pb.h"
#include "control.pb.h"
#include "events.pb.h"
#include "forever_module/talent_spec.h"
#include "handshake.pb.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{

int g_failures = 0;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            ++g_failures;                                                           \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
        }                                                                           \
    } while (0)

} // namespace

int main()
{
    // SDK -> core: HandshakeRequest wrapped in an Envelope.
    {
        forever_module::Envelope sdk;
        sdk.set_message_id(5);
        forever_module::HandshakeRequest* req = sdk.mutable_handshake_request();
        req->set_module_name("minimal-module");
        req->set_module_version("0.1.0");
        req->set_protocol_version("5");
        req->add_tier1_hooks("WorldScript.OnStartup");
        req->add_tier1_hooks("CommandScript");
        req->set_sdk_package_version("0.1.0");
        req->set_tier2_participates(true);
        req->set_requested_max_bots(64);
        req->set_requested_tick_interval(2);

        std::string bytes;
        CHECK(sdk.SerializeToString(&bytes));

        ModuleWire::Envelope env;
        CHECK(ModuleWire::DecodeEnvelope(bytes, env));
        CHECK(env.MessageId == 5);
        CHECK(env.Kind == ModuleWire::PayloadKind::HandshakeRequest);

        ModuleWire::HandshakeRequest core;
        CHECK(ModuleWire::Decode(env.Payload, core));
        CHECK(core.ModuleName == "minimal-module");
        CHECK(core.ModuleVersion == "0.1.0");
        CHECK(core.ProtocolVersion == "5");
        CHECK(core.Tier1Hooks.size() == 2);
        CHECK(core.Tier1Hooks.size() == 2 && core.Tier1Hooks[0] == "WorldScript.OnStartup");
        CHECK(core.Tier1Hooks.size() == 2 && core.Tier1Hooks[1] == "CommandScript");
        CHECK(core.SdkPackageVersion == "0.1.0");
        CHECK(core.Tier2Participates);
        CHECK(core.RequestedMaxBots == 64);
        CHECK(core.RequestedTickInterval == 2);
    }

    // SDK -> core: Heartbeat.
    {
        forever_module::Envelope sdk;
        sdk.mutable_heartbeat()->set_sequence(1234567890123ULL);
        std::string bytes;
        CHECK(sdk.SerializeToString(&bytes));

        ModuleWire::Envelope env;
        CHECK(ModuleWire::DecodeEnvelope(bytes, env));
        CHECK(env.Kind == ModuleWire::PayloadKind::Heartbeat);
        ModuleWire::Heartbeat hb;
        CHECK(ModuleWire::Decode(env.Payload, hb));
        CHECK(hb.Sequence == 1234567890123ULL);
    }

    // core -> SDK: HandshakeResponse (accepted, with negotiated bounds).
    {
        ModuleWire::HandshakeResponse resp;
        resp.Accepted = true;
        resp.ModuleId = 7;
        resp.NegotiatedMaxBots = 16;
        resp.NegotiatedTickInterval = 2;
        std::string bytes = ModuleWire::EncodeEnvelope(ModuleWire::PayloadKind::HandshakeResponse,
                                                       ModuleWire::Encode(resp), 3);

        forever_module::Envelope sdk;
        CHECK(sdk.ParseFromString(bytes));
        CHECK(sdk.message_id() == 3);
        CHECK(sdk.payload_case() == forever_module::Envelope::kHandshakeResponse);
        CHECK(sdk.handshake_response().accepted());
        CHECK(sdk.handshake_response().module_id() == 7);
        CHECK(sdk.handshake_response().negotiated_max_bots() == 16);
        CHECK(sdk.handshake_response().negotiated_tick_interval() == 2);
        CHECK(sdk.handshake_response().reason().empty());
    }

    // core -> SDK: rejection carries the reason.
    {
        ModuleWire::HandshakeResponse resp;
        resp.Accepted = false;
        resp.Reason = "protocol_version mismatch";
        std::string bytes = ModuleWire::EncodeEnvelope(ModuleWire::PayloadKind::HandshakeResponse,
                                                       ModuleWire::Encode(resp));
        forever_module::Envelope sdk;
        CHECK(sdk.ParseFromString(bytes));
        CHECK(!sdk.handshake_response().accepted());
        CHECK(sdk.handshake_response().reason() == "protocol_version mismatch");
    }

    // core -> SDK: ModuleConfig with two entries.
    {
        std::vector<ModuleWire::ConfigEntry> values = {{"greeting", "hello"}, {"empty", ""}};
        std::string bytes = ModuleWire::EncodeEnvelope(ModuleWire::PayloadKind::ModuleConfig,
                                                       ModuleWire::Encode(values));
        forever_module::Envelope sdk;
        CHECK(sdk.ParseFromString(bytes));
        CHECK(sdk.payload_case() == forever_module::Envelope::kModuleConfig);
        CHECK(sdk.module_config().values_size() == 2);
        CHECK(sdk.module_config().values(0).key() == "greeting");
        CHECK(sdk.module_config().values(0).value() == "hello");
        CHECK(sdk.module_config().values(1).key() == "empty");
        CHECK(sdk.module_config().values(1).value().empty());
    }

    // core -> SDK: Shutdown with a reason.
    {
        ModuleWire::Shutdown shutdown{"server stopping"};
        std::string bytes = ModuleWire::EncodeEnvelope(ModuleWire::PayloadKind::Shutdown,
                                                       ModuleWire::Encode(shutdown));
        forever_module::Envelope sdk;
        CHECK(sdk.ParseFromString(bytes));
        CHECK(sdk.payload_case() == forever_module::Envelope::kShutdown);
        CHECK(sdk.shutdown().reason() == "server stopping");
    }

    // Unknown fields are skipped, as protobuf does: append an unknown varint field (number 99) and an
    // unknown length-delimited field (number 98) to a heartbeat envelope.
    {
        std::string bytes = ModuleWire::EncodeEnvelope(ModuleWire::PayloadKind::Heartbeat,
                                                       ModuleWire::Encode(ModuleWire::Heartbeat{42}));
        bytes += std::string("\xD8\x06\x05", 3);          // field 99, varint, value 5
        bytes += std::string("\xC2\x06\x02hi", 5);         // field 98, length-delimited, "hi"

        ModuleWire::Envelope env;
        CHECK(ModuleWire::DecodeEnvelope(bytes, env));
        CHECK(env.Kind == ModuleWire::PayloadKind::Heartbeat);
        ModuleWire::Heartbeat hb;
        CHECK(ModuleWire::Decode(env.Payload, hb));
        CHECK(hb.Sequence == 42);

        forever_module::Envelope sdk;
        CHECK(sdk.ParseFromString(bytes));
        CHECK(sdk.heartbeat().sequence() == 42);
    }

    // Malformed input is rejected rather than read past the end.
    {
        ModuleWire::Envelope env;
        CHECK(!ModuleWire::DecodeEnvelope(std::string("\x0A\x10", 2), env)); // length 16, no bytes
        CHECK(!ModuleWire::DecodeEnvelope(std::string("\xFF\xFF\xFF", 3), env)); // truncated varint
    }

    // core -> SDK: EventNotify for every hook payload kind the core sends.
    {
        struct Case { ModuleWire::NotifyKind kind; uint64 value; };
        Case const cases[] = {
            {ModuleWire::NotifyKind::PlayerLogin, 42},
            {ModuleWire::NotifyKind::PlayerLogout, 43},
            {ModuleWire::NotifyKind::AccountLogin, 7},
            {ModuleWire::NotifyKind::WorldStartup, 0},
            {ModuleWire::NotifyKind::WorldShutdown, 0},
        };
        for (Case const& c : cases)
        {
            std::string bytes = ModuleWire::EncodeEnvelope(ModuleWire::PayloadKind::EventNotify,
                                                           ModuleWire::EncodeEventNotify("Hook.Name", c.kind, c.value));
            forever_module::Envelope sdk;
            CHECK(sdk.ParseFromString(bytes));
            CHECK(sdk.payload_case() == forever_module::Envelope::kEventNotify);
            CHECK(sdk.event_notify().hook_name() == "Hook.Name");
            switch (c.kind)
            {
                case ModuleWire::NotifyKind::PlayerLogin:
                    CHECK(sdk.event_notify().event_payload_case() == forever_module::EventNotify::kPlayerLogin);
                    CHECK(sdk.event_notify().player_login().player_guid() == c.value);
                    break;
                case ModuleWire::NotifyKind::PlayerLogout:
                    CHECK(sdk.event_notify().event_payload_case() == forever_module::EventNotify::kPlayerLogout);
                    CHECK(sdk.event_notify().player_logout().player_guid() == c.value);
                    break;
                case ModuleWire::NotifyKind::AccountLogin:
                    CHECK(sdk.event_notify().event_payload_case() == forever_module::EventNotify::kAccountLogin);
                    CHECK(sdk.event_notify().account_login().account_id() == c.value);
                    break;
                case ModuleWire::NotifyKind::WorldStartup:
                    CHECK(sdk.event_notify().event_payload_case() == forever_module::EventNotify::kWorldStartup);
                    break;
                case ModuleWire::NotifyKind::WorldShutdown:
                    CHECK(sdk.event_notify().event_payload_case() == forever_module::EventNotify::kWorldShutdown);
                    break;
                case ModuleWire::NotifyKind::PlayerChat:
                    break; // has its own body, checked below
            }
        }
    }

    // core -> SDK: PlayerChat notify.
    {
        ModuleWire::ChatEvent chat;
        chat.PlayerGuid = 55;
        chat.Channel = 3;
        chat.Text = "follow";
        chat.RecipientGuid = 66;
        chat.MapId = 36;
        chat.PosX = 1.5f;
        chat.PosY = -2.5f;
        chat.PosZ = 3.25f;
        chat.TargetRef = 99;
        std::string bytes = ModuleWire::EncodeEnvelope(ModuleWire::PayloadKind::EventNotify,
                                                       ModuleWire::EncodeEventNotifyChat("PlayerScript.OnChat", chat));
        forever_module::Envelope sdk;
        CHECK(sdk.ParseFromString(bytes));
        CHECK(sdk.event_notify().hook_name() == "PlayerScript.OnChat");
        CHECK(sdk.event_notify().event_payload_case() == forever_module::EventNotify::kPlayerChat);
        CHECK(sdk.event_notify().player_chat().player_guid() == 55);
        CHECK(sdk.event_notify().player_chat().channel() == 3);
        CHECK(sdk.event_notify().player_chat().text() == "follow");
        CHECK(sdk.event_notify().player_chat().recipient_guid() == 66);
        CHECK(sdk.event_notify().player_chat().map_id() == 36);
        CHECK(sdk.event_notify().player_chat().pos_x() == 1.5f && sdk.event_notify().player_chat().pos_y() == -2.5f &&
              sdk.event_notify().player_chat().pos_z() == 3.25f);
        CHECK(sdk.event_notify().player_chat().target_ref() == 99);
    }

    // core -> SDK: EventRequest carrying a command, with the envelope message id set.
    {
        std::string bytes = ModuleWire::EncodeEnvelope(ModuleWire::PayloadKind::EventRequest,
                                                       ModuleWire::EncodeEventRequestCommand(77, "CommandScript", "ping", 1234), 77);
        forever_module::Envelope sdk;
        CHECK(sdk.ParseFromString(bytes));
        CHECK(sdk.message_id() == 77);
        CHECK(sdk.payload_case() == forever_module::Envelope::kEventRequest);
        CHECK(sdk.event_request().message_id() == 77);
        CHECK(sdk.event_request().hook_name() == "CommandScript");
        CHECK(sdk.event_request().command().command_text() == "ping");
        CHECK(sdk.event_request().command().issuer_guid() == 1234);
    }

    // SDK -> core: EventReply with and without a command payload.
    {
        forever_module::EventReply reply;
        reply.set_message_id(77);
        reply.mutable_command()->set_success(true);
        reply.mutable_command()->set_result_text("pong");
        std::string bytes;
        CHECK(reply.SerializeToString(&bytes));

        ModuleWire::EventReplyMsg core;
        CHECK(ModuleWire::Decode(bytes, core));
        CHECK(core.MessageId == 77);
        CHECK(core.HasCommand);
        CHECK(core.Success);
        CHECK(core.ResultText == "pong");

        forever_module::EventReply failed;
        failed.set_message_id(78);
        failed.mutable_command()->set_success(false);
        std::string failedBytes;
        CHECK(failed.SerializeToString(&failedBytes));
        ModuleWire::EventReplyMsg core2;
        CHECK(ModuleWire::Decode(failedBytes, core2));
        CHECK(core2.MessageId == 78 && core2.HasCommand && !core2.Success && core2.ResultText.empty());

        forever_module::EventReply empty;
        empty.set_message_id(79);
        std::string emptyBytes;
        CHECK(empty.SerializeToString(&emptyBytes));
        ModuleWire::EventReplyMsg core3;
        CHECK(ModuleWire::Decode(emptyBytes, core3));
        CHECK(core3.MessageId == 79 && !core3.HasCommand);
    }

    // SDK -> core: SellItem and SendPlayerMessage action requests.
    {
        forever_module::Envelope sdk;
        sdk.set_message_id(31);
        forever_module::ActionRequest* req = sdk.mutable_action_request();
        req->set_message_id(31);
        forever_module::SellItemAction* sell = req->mutable_sell_item();
        sell->set_player_guid(42);
        sell->set_item_guid(5001);
        sell->set_item_entry(2589);
        sell->set_count(3);
        std::string bytes;
        CHECK(sdk.SerializeToString(&bytes));

        ModuleWire::Envelope env;
        CHECK(ModuleWire::DecodeEnvelope(bytes, env));
        CHECK(env.Kind == ModuleWire::PayloadKind::ActionRequest);
        CHECK(env.MessageId == 31);
        ModuleWire::ActionRequestMsg core;
        CHECK(ModuleWire::Decode(env.Payload, core));
        CHECK(core.MessageId == 31);
        CHECK(core.Kind == ModuleWire::ActionKind::SellItem);
        CHECK(core.PlayerGuid == 42);
        CHECK(core.ItemGuid == 5001);
        CHECK(core.ItemEntry == 2589);
        CHECK(core.Count == 3);

        forever_module::ActionRequest msg;
        msg.set_message_id(32);
        msg.mutable_send_message()->set_player_guid(42);
        msg.mutable_send_message()->set_text("sold for 60 copper");
        std::string msgBytes;
        CHECK(msg.SerializeToString(&msgBytes));
        ModuleWire::ActionRequestMsg core2;
        CHECK(ModuleWire::Decode(msgBytes, core2));
        CHECK(core2.MessageId == 32);
        CHECK(core2.Kind == ModuleWire::ActionKind::SendPlayerMessage);
        CHECK(core2.PlayerGuid == 42);
        CHECK(core2.Text == "sold for 60 copper");
    }

    // SDK -> core: ShareQuestItem action request.
    {
        forever_module::ActionRequest msg;
        msg.set_message_id(33);
        forever_module::ShareQuestItemAction* share = msg.mutable_share_quest_item();
        share->set_player_guid(42);
        share->set_item_entry(3000);
        share->set_count(1);
        share->set_range_yards(80);
        std::string bytes;
        CHECK(msg.SerializeToString(&bytes));
        ModuleWire::ActionRequestMsg core;
        CHECK(ModuleWire::Decode(bytes, core));
        CHECK(core.MessageId == 33);
        CHECK(core.Kind == ModuleWire::ActionKind::ShareQuestItem);
        CHECK(core.PlayerGuid == 42);
        CHECK(core.ItemEntry == 3000);
        CHECK(core.Count == 1);
        CHECK(core.RangeYards == 80);
    }

    // SDK -> core: Log action request.
    {
        forever_module::ActionRequest msg;
        msg.set_message_id(34);
        forever_module::LogAction* log = msg.mutable_log();
        log->set_level(1);
        log->set_text("42 items sold in the last 10 minutes. Total value 1g 20s 5c.");
        std::string bytes;
        CHECK(msg.SerializeToString(&bytes));
        ModuleWire::ActionRequestMsg core;
        CHECK(ModuleWire::Decode(bytes, core));
        CHECK(core.MessageId == 34);
        CHECK(core.Kind == ModuleWire::ActionKind::Log);
        CHECK(core.LogLevel == 1);
        CHECK(core.Text == "42 items sold in the last 10 minutes. Total value 1g 20s 5c.");
    }

    // Core -> SDK: ActionReply inside an Envelope, parsed by the SDK's protobuf.
    {
        ModuleWire::ActionReplyMsg reply;
        reply.MessageId = 31;
        reply.Success = true;
        reply.CopperGained = 60;
        reply.Recipients = 2;
        std::string bytes = ModuleWire::EncodeEnvelope(ModuleWire::PayloadKind::ActionReply, ModuleWire::Encode(reply), 31);

        forever_module::Envelope sdk;
        CHECK(sdk.ParseFromString(bytes));
        CHECK(sdk.payload_case() == forever_module::Envelope::kActionReply);
        CHECK(sdk.message_id() == 31);
        CHECK(sdk.action_reply().message_id() == 31);
        CHECK(sdk.action_reply().success());
        CHECK(sdk.action_reply().copper_gained() == 60);
        CHECK(sdk.action_reply().recipients() == 2);
        CHECK(!sdk.action_reply().has_reason());

        ModuleWire::ActionReplyMsg failed;
        failed.MessageId = 32;
        failed.Reason = "item is gone";
        std::string failedBytes = ModuleWire::Encode(failed);
        forever_module::ActionReply parsed;
        CHECK(parsed.ParseFromString(failedBytes));
        CHECK(parsed.message_id() == 32);
        CHECK(!parsed.success());
        CHECK(parsed.reason() == "item is gone");
        CHECK(parsed.copper_gained() == 0);
    }

    // Core -> SDK: PlayerItemLooted notify.
    {
        ModuleWire::ItemLootedEvent loot;
        loot.PlayerGuid = 42;
        loot.ItemGuid = 5001;
        loot.ItemEntry = 2589;
        loot.Count = 3;
        loot.Quality = 0;
        loot.ItemClass = 7;
        loot.SellPrice = 20;
        loot.ItemName = "Tattered Cloth";
        std::string body = ModuleWire::EncodeEventNotifyItemLooted("PlayerScript.OnLootItem", loot);

        forever_module::EventNotify notify;
        CHECK(notify.ParseFromString(body));
        CHECK(notify.hook_name() == "PlayerScript.OnLootItem");
        CHECK(notify.event_payload_case() == forever_module::EventNotify::kPlayerItemLooted);
        forever_module::PlayerItemLootedEvent const& sdk = notify.player_item_looted();
        CHECK(sdk.player_guid() == 42);
        CHECK(sdk.item_guid() == 5001);
        CHECK(sdk.item_entry() == 2589);
        CHECK(sdk.count() == 3);
        CHECK(sdk.quality() == 0);
        CHECK(sdk.item_class() == 7);
        CHECK(sdk.sell_price() == 20);
        CHECK(sdk.item_name() == "Tattered Cloth");
    }

    // SDK -> core: ControlRequest (spawn) and ReleaseControl.
    {
        forever_module::Envelope sdk;
        sdk.set_message_id(41);
        forever_module::ControlRequest* req = sdk.mutable_control_request();
        req->set_bot_guid(0);
        req->mutable_spawn_params()->set_race(2);
        req->mutable_spawn_params()->set_class_id(3);
        req->mutable_spawn_params()->set_level(80);
        req->mutable_spawn_params()->set_team(1);
        req->mutable_spawn_params()->set_place(true);
        req->mutable_spawn_params()->set_map_id(36);
        req->mutable_spawn_params()->set_pos_x(-16.5f);
        req->mutable_spawn_params()->set_pos_y(-383.0f);
        req->mutable_spawn_params()->set_pos_z(61.8f);
        req->mutable_spawn_params()->set_alt_name("Thrall");
        req->mutable_spawn_params()->set_owner_guid(77);
        std::string bytes;
        CHECK(sdk.SerializeToString(&bytes));

        ModuleWire::Envelope env;
        CHECK(ModuleWire::DecodeEnvelope(bytes, env));
        CHECK(env.Kind == ModuleWire::PayloadKind::ControlRequest);
        CHECK(env.MessageId == 41);
        ModuleWire::ControlRequestMsg core;
        CHECK(ModuleWire::Decode(env.Payload, core));
        CHECK(core.BotGuid == 0);
        CHECK(core.SpawnParams.Race == 2 && core.SpawnParams.ClassId == 3);
        CHECK(core.SpawnParams.Level == 80 && core.SpawnParams.Team == 1);
        CHECK(core.SpawnParams.Place && core.SpawnParams.MapId == 36);
        CHECK(core.SpawnParams.X == -16.5f && core.SpawnParams.Y == -383.0f && core.SpawnParams.Z == 61.8f);
        CHECK(core.SpawnParams.AltName == "Thrall" && core.SpawnParams.OwnerGuid == 77);

        forever_module::Envelope release;
        release.mutable_release_control()->set_bot_guid(5005);
        std::string releaseBytes;
        CHECK(release.SerializeToString(&releaseBytes));
        ModuleWire::Envelope releaseEnv;
        CHECK(ModuleWire::DecodeEnvelope(releaseBytes, releaseEnv));
        CHECK(releaseEnv.Kind == ModuleWire::PayloadKind::ReleaseControl);
        ModuleWire::ReleaseControlMsg coreRelease;
        CHECK(ModuleWire::Decode(releaseEnv.Payload, coreRelease));
        CHECK(coreRelease.BotGuid == 5005);
    }

    // Core -> SDK: ControlResponse (granted and refused) and ControlRevoked.
    {
        ModuleWire::ControlResponseMsg granted;
        granted.Granted = true;
        granted.BotGuid = 5005;
        granted.Race = 2;
        granted.ClassId = 7;
        std::string bytes = ModuleWire::EncodeEnvelope(ModuleWire::PayloadKind::ControlResponse, ModuleWire::Encode(granted), 41);
        forever_module::Envelope sdk;
        CHECK(sdk.ParseFromString(bytes));
        CHECK(sdk.payload_case() == forever_module::Envelope::kControlResponse);
        CHECK(sdk.message_id() == 41);
        CHECK(sdk.control_response().granted());
        CHECK(sdk.control_response().bot_guid() == 5005);
        CHECK(sdk.control_response().race() == 2 && sdk.control_response().class_id() == 7);
        CHECK(!sdk.control_response().has_reason());

        ModuleWire::ControlResponseMsg refused;
        refused.Reason = "already controlled by module other";
        forever_module::ControlResponse parsed;
        CHECK(parsed.ParseFromString(ModuleWire::Encode(refused)));
        CHECK(!parsed.granted());
        CHECK(parsed.reason() == "already controlled by module other");

        ModuleWire::ControlRevokedMsg revoked;
        revoked.BotGuid = 5005;
        revoked.Reason = "bot deleted";
        std::string revokedBytes = ModuleWire::EncodeEnvelope(ModuleWire::PayloadKind::ControlRevoked, ModuleWire::Encode(revoked));
        forever_module::Envelope revokedSdk;
        CHECK(revokedSdk.ParseFromString(revokedBytes));
        CHECK(revokedSdk.payload_case() == forever_module::Envelope::kControlRevoked);
        CHECK(revokedSdk.control_revoked().bot_guid() == 5005);
        CHECK(revokedSdk.control_revoked().reason() == "bot deleted");
    }

    // SDK -> core: BotSpeakAction (actions.proto field 20).
    {
        forever_module::ActionRequest sdk;
        sdk.set_message_id(77);
        forever_module::BotSpeakAction* speak = sdk.mutable_bot_speak();
        speak->set_bot_guid(42);
        speak->set_kind(1);
        speak->set_player_guid(7);
        speak->set_text("Hello there");
        std::string bytes;
        CHECK(sdk.SerializeToString(&bytes));

        ModuleWire::ActionRequestMsg core;
        CHECK(ModuleWire::Decode(bytes, core));
        CHECK(core.MessageId == 77 && core.Kind == ModuleWire::ActionKind::BotSpeak);
        CHECK(core.BotGuid == 42 && core.SpeakKind == 1 && core.PlayerGuid == 7 && core.Text == "Hello there");
    }

    // SDK -> core: SpendBotTalentsAction (actions.proto field 22).
    for (std::uint32_t role = 0; role <= 3; ++role)
    {
        forever_module::ActionRequest sdk;
        sdk.set_message_id(200 + role);
        forever_module::SpendBotTalentsAction* talents = sdk.mutable_spend_bot_talents();
        talents->set_bot_guid(42);
        talents->set_role(role);
        std::string bytes;
        CHECK(sdk.SerializeToString(&bytes));

        ModuleWire::ActionRequestMsg core;
        CHECK(ModuleWire::Decode(bytes, core));
        CHECK(core.MessageId == 200 + role && core.Kind == ModuleWire::ActionKind::SpendBotTalents);
        CHECK(core.BotGuid == 42 && core.TalentRole == role && core.TalentSpec == 0); // no spec on the wire: 0 (protocol 17 callers)
    }
    // ... with a spec (protocol 18): every spec value of the SDK table decodes, with role and bot guid unchanged.
    for (std::uint32_t spec = 1; spec <= forever_module::kLastTalentSpec; ++spec)
    {
        forever_module::ActionRequest sdk;
        sdk.set_message_id(300 + spec);
        forever_module::SpendBotTalentsAction* talents = sdk.mutable_spend_bot_talents();
        talents->set_bot_guid(77);
        talents->set_role(spec % 4);
        talents->set_spec(spec);
        std::string bytes;
        CHECK(sdk.SerializeToString(&bytes));

        ModuleWire::ActionRequestMsg core;
        CHECK(ModuleWire::Decode(bytes, core));
        CHECK(core.MessageId == 300 + spec && core.Kind == ModuleWire::ActionKind::SpendBotTalents);
        CHECK(core.BotGuid == 77 && core.TalentRole == spec % 4 && core.TalentSpec == spec);
    }
    // The core's spec table (Bots/BotTalentSpec.h) equals the SDK's (talent_spec.h): id, class and key of every row.
    CHECK(BotTalentSpec::LastSpec == forever_module::kLastTalentSpec);
    for (std::uint32_t id = 1; id <= forever_module::kLastTalentSpec; ++id)
    {
        forever_module::TalentSpecInfo const* sdkInfo = forever_module::GetTalentSpecInfo(static_cast<forever_module::TalentSpec>(id));
        BotTalentSpec::Info const* coreInfo = BotTalentSpec::Get(id);
        CHECK(sdkInfo && coreInfo);
        if (sdkInfo && coreInfo)
            CHECK(sdkInfo->ClassId == coreInfo->ClassId && std::string(sdkInfo->Key) == coreInfo->Key && coreInfo->Spec == id);
    }

    // SDK -> core: BotTradeAction (field 21), every operation.
    for (int op = 0; op <= 6; ++op)
    {
        forever_module::ActionRequest sdk;
        sdk.set_message_id(100 + op);
        forever_module::BotTradeAction* trade = sdk.mutable_bot_trade();
        trade->set_op(static_cast<forever_module::BotTradeAction_TradeOp>(op));
        trade->set_bot_guid(42);
        trade->set_slot(3);
        trade->set_item_guid(123456789012ULL);
        trade->set_copper(5000);
        std::string bytes;
        CHECK(sdk.SerializeToString(&bytes));

        ModuleWire::ActionRequestMsg core;
        CHECK(ModuleWire::Decode(bytes, core));
        CHECK(core.Kind == ModuleWire::ActionKind::BotTrade && core.TradeOp == uint32(op));
        CHECK(core.BotGuid == 42 && core.TradeSlot == 3 && core.ItemGuid == 123456789012ULL && core.Copper == 5000);
    }

    // core -> SDK: an ActionReply with bag items (field 12).
    {
        ModuleWire::ActionReplyMsg core;
        core.MessageId = 9;
        core.Success = true;
        core.BagItems.push_back({ 1001, 2589, 20, "Linen Cloth" });
        core.BagItems.push_back({ 1002, 6948, 1, "Hearthstone" });
        std::string bytes = ModuleWire::Encode(core);

        forever_module::ActionReply sdk;
        CHECK(sdk.ParseFromString(bytes));
        CHECK(sdk.success() && sdk.message_id() == 9 && sdk.bag_items_size() == 2);
        CHECK(sdk.bag_items(0).item_guid() == 1001 && sdk.bag_items(0).entry() == 2589 && sdk.bag_items(0).count() == 20);
        CHECK(sdk.bag_items(0).name() == "Linen Cloth" && sdk.bag_items(1).name() == "Hearthstone");
    }

    // SDK -> core: AuctionHouseAction browse by filter (actions.proto field 19, fields 10 to 15; protocol 19).
    {
        forever_module::ActionRequest sdk;
        sdk.set_message_id(61);
        forever_module::AuctionHouseAction* ah = sdk.mutable_auction_house();
        ah->set_op(forever_module::AuctionHouseAction::kBrowse);
        ah->set_auctioneer_guid(9002);
        ah->set_player_guid(42);
        ah->set_inventory_type(7);
        ah->set_min_level(5);
        ah->set_max_level(15);
        ah->set_max_results(8);
        ah->set_max_buyout(4000);
        ah->set_only_upgrades(true);
        std::string bytes;
        CHECK(sdk.SerializeToString(&bytes));

        ModuleWire::ActionRequestMsg core;
        CHECK(ModuleWire::Decode(bytes, core));
        CHECK(core.MessageId == 61 && core.Kind == ModuleWire::ActionKind::AuctionHouse && core.AuctionOp == 0 && core.AuctioneerGuid == 9002);
        CHECK(core.PlayerGuid == 42 && core.ItemEntry == 0);
        CHECK(core.AuctionInventoryType == 7 && core.AuctionMinLevel == 5 && core.AuctionMaxLevel == 15 && core.AuctionMaxResults == 8);
        CHECK(core.AuctionMaxBuyout == 4000 && core.AuctionOnlyUpgrades);
    }
    // ... and the old browse by item entry leaves the filter empty.
    {
        forever_module::ActionRequest sdk;
        sdk.set_message_id(62);
        forever_module::AuctionHouseAction* ah = sdk.mutable_auction_house();
        ah->set_op(forever_module::AuctionHouseAction::kBrowse);
        ah->set_auctioneer_guid(9002);
        ah->set_item_entry(2512);
        ah->set_player_guid(42);
        std::string bytes;
        CHECK(sdk.SerializeToString(&bytes));

        ModuleWire::ActionRequestMsg core;
        CHECK(ModuleWire::Decode(bytes, core));
        CHECK(core.ItemEntry == 2512 && core.AuctionInventoryType == 0 && core.AuctionMaxResults == 0 && !core.AuctionOnlyUpgrades);
    }

    // core -> SDK: an ActionReply with listings (field 13) and an AuctionListingEvent notify with the new fields (12 to 16).
    {
        ModuleWire::AuctionListingEvent listing;
        listing.AuctionGuid = 900;
        listing.ItemEntry = 2000;
        listing.Count = 1;
        listing.Bid = 50;
        listing.Buyout = 1500;
        listing.TimeLeftMin = 90;
        listing.Seller = "Seller";
        listing.TotalMatches = 3;
        listing.PlayerGuid = 42;
        listing.RequestId = 61;
        listing.InventoryType = 7;
        listing.Quality = 2;
        listing.RequiredLevel = 10;
        listing.ItemLevel = 13;
        listing.UpgradeScore = 31;

        ModuleWire::ActionReplyMsg core;
        core.MessageId = 61;
        core.Success = true;
        core.Listings.push_back(listing);
        listing.AuctionGuid = 901;
        listing.UpgradeScore = 0;
        core.Listings.push_back(listing);
        forever_module::ActionReply sdk;
        CHECK(sdk.ParseFromString(ModuleWire::Encode(core)));
        CHECK(sdk.success() && sdk.listings_size() == 2);
        forever_module::AuctionListingEvent const& first = sdk.listings(0);
        CHECK(first.auction_guid() == 900 && first.item_entry() == 2000 && first.count() == 1 && first.bid() == 50 && first.buyout() == 1500);
        CHECK(first.time_left_min() == 90 && first.seller() == "Seller" && first.total_matches() == 3 && first.player_guid() == 42 && first.request_id() == 61);
        CHECK(first.inventory_type() == 7 && first.quality() == 2 && first.required_level() == 10 && first.item_level() == 13 && first.upgrade_score() == 31);
        CHECK(sdk.listings(1).auction_guid() == 901 && !sdk.listings(1).has_upgrade_score());

        // the notify (the old browse) carries the same body
        forever_module::EventNotify notify;
        CHECK(notify.ParseFromString(ModuleWire::EncodeEventNotifyAuctionListing("PlayerScript.OnAuctionResult", listing)));
        CHECK(notify.hook_name() == "PlayerScript.OnAuctionResult" && notify.has_player_auction_result());
        CHECK(notify.player_auction_result().auction_guid() == 901 && notify.player_auction_result().item_level() == 13);
    }

    // core -> SDK: a BotTradeEvent (events.proto field 19) in an EventNotify.
    {
        ModuleWire::BotTradeEvent core;
        core.BotGuid = 42;
        core.PartnerGuid = 7;
        core.Status = 3;
        core.PartnerCopper = 150;
        core.Offered.push_back({ 0, 2589, 5 });
        core.Offered.push_back({ 2, 6948, 1 });
        std::string bytes = ModuleWire::EncodeEventNotifyBotTrade("PlayerScript.OnBotTrade", core);

        forever_module::EventNotify sdk;
        CHECK(sdk.ParseFromString(bytes));
        CHECK(sdk.hook_name() == "PlayerScript.OnBotTrade" && sdk.has_bot_trade());
        CHECK(sdk.bot_trade().bot_guid() == 42 && sdk.bot_trade().partner_guid() == 7 && sdk.bot_trade().status() == 3);
        CHECK(sdk.bot_trade().partner_copper() == 150 && sdk.bot_trade().offered_size() == 2);
        CHECK(sdk.bot_trade().offered(0).slot() == 0 && sdk.bot_trade().offered(0).entry() == 2589 && sdk.bot_trade().offered(0).count() == 5);
        CHECK(sdk.bot_trade().offered(1).slot() == 2 && sdk.bot_trade().offered(1).entry() == 6948);
    }

    if (g_failures == 0)
    {
        std::printf("PASS  ModuleWire interop with SDK protobuf\n");
        return 0;
    }
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
}
