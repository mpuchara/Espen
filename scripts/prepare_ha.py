#!/usr/bin/env python3
"""Stage 8 overlay on pinned Xiaozhi 2.5.2: add bounded read-only MCP worker.
Run AFTER prepare.py and prepare_phone.py. All anchors are fail-closed.
"""
from pathlib import Path
import shutil
import sys

root = Path(__file__).resolve().parents[1]
up = Path(sys.argv[1]).resolve()
board = up / "main/boards/guition/espen-guition-jc4880p443"
for name in ("ala_ha_policy.h", "ala_home_assistant.h", "ala_home_assistant_mcp.h"):
    shutil.copyfile(root / "firmware" / name, board / name)


def replace(path, before, after):
    text = path.read_text()
    if text.count(before) != 1:
        raise RuntimeError(f"Stage 8: missing or ambiguous anchor {path}: {before[:70]}")
    path.write_text(text.replace(before, after, 1))


cmake = up / "main/CMakeLists.txt"
replace(cmake, "                        esp_http_server\n",
        "                        esp_http_server\n                        esp_http_client\n")

app = up / "main/application.cc"
replace(app, '#include "mcp_server.h"',
        '#include "mcp_server.h"\n'
        '#include "boards/guition/espen-guition-jc4880p443/ala_home_assistant_mcp.h"')
replace(app, "    mcp_server.AddUserOnlyTools();",
        "    mcp_server.AddUserOnlyTools();\n"
        "    espen::RegisterHomeAssistantMcpTools(); // same existing Xiaozhi MCP channel")

mcp = up / "main/mcp_server.cc"
replace(mcp, "#include <optional>",
        "#include <optional>\n#include <atomic>\n#include <new>\n#include <freertos/task.h>")
# Pinned upstream schedules ALL MCP callbacks on Application's main task.
# A 4-second HA network call there would stall Xiaozhi audio/UI. Only HA
# callbacks are dispatched on a bounded FreeRTOS worker. The result still
# uses the original JSON-RPC ReplyResult/ReplyError machinery.
replace(mcp, "    // Use main thread to call the tool\n",
'''    // Ala Stage 8: dedicated worker for slow LOCAL HA I/O, never main/audio/LVGL.
    if (tool_name.rfind("self.home_assistant.", 0) == 0) {
        static std::atomic<bool> ha_busy{false};
        if (ha_busy.exchange(true, std::memory_order_acq_rel)) {
            ReplyError(id, -32603, "Home Assistant busy", response_sender);
            return;
        }
        struct HaJob {
            McpServer* server;
            int id;
            McpTool* tool;
            PropertyList properties;
            ResponseSender sender;
        };
        auto* job = new (std::nothrow) HaJob{this, id, tool, std::move(arguments),
                                            std::move(response_sender)};
        if (!job) {
            ha_busy.store(false, std::memory_order_release);
            ReplyError(id, -32603, "Home Assistant out of memory", response_sender);
            return;
        }
        const BaseType_t created = xTaskCreate([](void* arg) {
            std::unique_ptr<HaJob> job(static_cast<HaJob*>(arg));
            auto result = job->tool->Call(job->properties);
            if (!result) job->server->ReplyError(job->id, result.error(), job->sender);
            else job->server->ReplyResult(job->id, *result, job->sender);
            ha_busy.store(false, std::memory_order_release);
            job.reset();
            vTaskDelete(nullptr);
        }, "ala_ha_mcp", 9216, job, tskIDLE_PRIORITY + 2, nullptr);
        if (created != pdPASS) {
            ha_busy.store(false, std::memory_order_release);
            ReplyError(id, -32603, "Home Assistant worker failed", job->sender);
            delete job;
        }
        return;
    }

    // Use main thread to call the tool
''')
print("Stage 8 MCP read-only Home Assistant overlay applied")
