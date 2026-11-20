# Ala Stage 6 — Personal Memory Engine

Base: PR #41, branch feature/ala-native-xiaozhi-social-stage5-2, commit 080d1c9223aa76d75cd6f4f4528753a916167857. This is a stacked, experimental change; PR #39 and #40 are excluded.

## Delivered capabilities

AlaMemoryEngine is a local, bounded family memory, disabled by default. Up to **20 entries of 160 UTF-8 bytes each** can be stored in categories: Preferences, Interests, Robot, Agreements and Facts. There is no speaker recognition and no automatic profile-building. Only approved edits write flash. Duplicate texts are rejected (case and basic Polish diacritics folded). Invalid UTF-8, control characters and selected obvious secrets/medical terms are rejected. These are best-effort safeguards, not a guarantee that any entered text is non-sensitive.

NVS namespace: ala_memory, key: snapshot. Schema 1 stores item id/category/length/text and FNV-1a checksum in one NVS blob, committed once per confirmed mutation. The old RAM state remains if a write fails. Corrupt or unknown-version blobs fail closed with memory OFF and an error; they are not overwritten on read. Upper size limit: 4096 bytes; 20 fully occupied entries need **3271 bytes of blob payload**, plus NVS overhead. Actual allocated RAM and NVS footprint depend on ESP-IDF / allocator and still need device measurement.

Phone panel (existing local HTTP server and eight-digit PIN, same X-Ala-Pin authorization): GET and POST /api/memory support enable/disable, view, add, edit, delete, clear, import, export and explicit questions. OFF blocks searching, adding and sending; it retains local entries until explicitly cleared. Clear requires two UI confirmations. JSON API responses are no-store; memory is not logged. HTTP itself is **not encrypted**.

Export downloads ala-memory.alamemory as printable ALAM1: + HEX with integrity-checked snapshot. Import validates schema, checksum, duplicates, IDs and UTF-8 before replacing entries, and **never silently enables memory**. The user must back up before flashing the full espen.bin; a full install may erase NVS. Copies stay local and are not automatically uploaded to a cloud.

## Xiaozhi and voice limitations

Only the existing Xiaozhi STT, TTS and conversational AI are used. Stage 6 hooks already delivered role=user transcriptions from the existing session to a **candidate proposal**, not an automatic write. A complete direct speech command such as “Ala zapamiętaj że lubię planety” can yield a candidate in the phone panel; it is only persisted after clicking confirmation with PIN. Quoted commands, incidental speech, background noise and TV cannot reliably be assigned to a speaker; they never cause automatic persistence.

The panel action “Zapytaj Xiaozhi — wyrażam zgodę” requires a user confirmation and sends **at most three locally selected approved memories** with a question using the existing EspenAskText path. No periodic full-memory upload. Because public Xiaozhi can respond before local STT processing and does not guarantee arbitrary memory injection into its normal LLM turns, **automatic spoken answers to “Co pamiętasz?” are NOT implemented or validated**. Standalone spoken forgetting is not advertised. Use the authenticated phone panel to delete memories. No new backend, OpenAI key, LLM, STT or TTS is required. No change to Xiaozhi server or protocol.

These routines do not alter robot audio, Wi-Fi, drivers, wake-word, barge-in, facial animations, persistent phone emotions, Living Robot Engine, or the verified fail-closed ban on unsolicited social comments in PR #41.

## Automated verification

Host C++ tests: tests/test_ala_memory_engine.cc — initial OFF, opt-in, add/lookup/edit/delete/clear, duplicate detection, 20-item limit, UTF-8 validation, prohibited data, voice proposal without flash write, reboot, export/import, CRC and version failures, simulated NVS write failure.

Integration checks: tests/test_memory_contracts.py and tests/test_phone_panel.py verify the real patching and PIN-authenticated endpoints. scripts/build.sh also runs host tests before the pinned Xiaozhi ESP32-P4 build. Both GitHub Actions workflows include the PR #41 branch, and the firmware workflow checks the full 16 MB espen.bin and SHA256. **A successful compilation is not a device test.**

## Physical ESP32-P4 checklist — NOT TESTED

1. Open local phone panel with PIN. Memory is OFF and empty by default. Enable and add “Lubię astronomię”.
2. Restart: verify saved entry and ON state remain. Edit it and delete it. Restart again to ensure it stays deleted.
3. Say “Ala zapamiętaj że lubię planety”. Confirm a *proposal* appears. Verify no persistence without clicking confirmation; accept via PIN panel.
4. When Xiaozhi is idle and connected, click the explicit ask button with a matching query; verify actual assistant output. Only up to 3 matches are sent.
5. Disable: search/ask/add must be blocked, but entries remain. Test that disabled state persists through reboot.
6. Export a backup; perform a **full espen.bin install**; import; verify entries. Confirm import cannot enable memory itself.
7. Regression: Xiaozhi speech, wake word, barge-in, “bez odbioru”, camera, sticky phone emotions, Social mode, animations, frame rate, no bootloop.
8. Invalid PIN, oversized text, invalid backup and intentionally interrupted power during a memory edit must fail safely.

Security note: HTTP and NVS are not secure stores for secrets; deletion does not imply forensic erasure of old flash blocks. Keep exported backups private.