#pragma once
// MCP tools for the pinned Xiaozhi v2.5.2 API. No alternative STT/TTS/LLM.
#include "mcp_server.h"
#include "ala_home_assistant.h"

namespace espen {
inline void RegisterHomeAssistantMcpTools() {
    auto& mcp = McpServer::GetInstance();
    mcp.AddTool(
        "self.home_assistant.list_allowed_entities",
        "List only the Home Assistant sensors/devices explicitly approved by the owner. "
        "Do not guess entity IDs or invent values. Home Assistant is optional.",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            return AlaHomeAssistant::Instance().Status(true);
        });
    mcp.AddTool(
        "self.home_assistant.get_state",
        "Read the CURRENT real state of one approved Home Assistant entity. "
        "First use list_allowed_entities to find an exact entity_id. "
        "Never claim measurements when ok=false, available=false or the tool fails. "
        "The result may contain a unit and last_updated from Home Assistant.",
        PropertyList({Property("entity_id",kPropertyTypeString).SetMaxLength(95)}),
        [](const PropertyList& args) -> ReturnValue {
            return AlaHomeAssistant::Instance().Read(args["entity_id"].value<std::string>());
        });
    // No control tool: light control requires explicit local confirmation UX.
}
}
