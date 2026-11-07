# R3 validation record

| Run | Result | Exit |
|---|---:|---:|
| Debug build, all 20 configured application/test/module targets (final recovery-test revision) | PASS | 0 |
| Binding | 16/16 PASS | 0 |
| Authoring | 11/11 PASS | 0 |
| UV | 286/286 PASS | 0 |
| Lifecycle | 13/13 PASS | 0 |
| 326-test total | 326/326 PASS | 0 |
| Recovery callback fault-injection cases | 3/3 PASS | 0 |
| Lifecycle Stability repetitions | 5/5 PASS, each runner 13/13 | 0 each |
| GUI: three Hub → Block Maker → Painter → Hub cycles | PASS on immediately prior renderer build | 0 |
| GUI: two resize actions | PASS on immediately prior renderer build | 0 |
| GUI normal shutdown, Twitch disabled | PASS; 6 semaphores created/destroyed, 0 live; validation errors 0 | 0 |
| GUI persistence reopen | PASS; paint layer reloaded and visible | 0 |
| GUI, Twitch enabled A/B | Reproduces 13 validation errors and four device-child semaphore leaks | 0 |

The GUI, persistence, and Twitch A/B runs were completed before the final narrow recovery-callback test-seam revision and were not repeated afterward. The final revision was rebuilt across all 20 targets and all automated suites rerun. Computer Use then reported that it had been stopped by the physical Escape key, so no further GUI run was attempted.

`VK_LAYER_KHRONOS_validation` was active and the Debug Messenger registered in both A/B runs. Twitch-disabled shutdown was clean with zero validation errors. Twitch-enabled run reported 13 validation errors: six `VUID-VkFramebufferCreateInfo-renderPass-parameter`, six `UNASSIGNED-GeneralParameterError-RequiredHandle`, and one `VUID-vkDestroyDevice-device-05137`. Twitch-enabled leak handles: `0x1a000000001a`, `0x1b000000001b`, `0xce00000000ce`, `0xcf00000000cf`. The device-destruction callback stack includes Twitch proc-address/message callbacks, but creation traces for these four handles are absent; no ownership is attributed to Twitch.

The recovery tests inject three `VkResult` combinations through the same callback wrapper used by `RenderManager`: device lost skips recovery, successful recovery invokes the submit callback exactly once and consumes acquire/signals the fence, failed recovery invokes it once and quarantines acquire/avoids waiting on an unsignaled fence. This is callback/policy-level fault injection, not a Vulkan-driver fault injection run.

The full Debug build and final automated logs are stored alongside this report. The GUI-generated paint data and test packaging mutation remain in the working tree; an automatic review rejected the cleanup operation, so those artifacts were preserved rather than removed.

No P0.3C3 freeze declaration is made.
