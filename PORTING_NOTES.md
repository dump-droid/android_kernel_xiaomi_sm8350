# Xiaomi Pad 6 (pipa) 5.4 Kernel Porting Notes

## Ported Subsystems
1. **Device Tree (DTS/DTSI):**
   - The device tree for `pipa` and the `kona` SoC was completely missing from the `4.19` kernel tree.
   - It was cloned separately from `MiCode/kernel_devicetree` and placed into `arch/arm64/boot/dts/vendor/qcom/`.
   - Added `CONFIG_ARCH_KONA` to `arch/arm64/Kconfig.platforms` and wired up `arch/arm64/boot/dts/vendor/qcom/Makefile` to build `pipa-sm8250-overlay.dtbo`.

2. **Defconfig:**
   - Copied `arch/arm64/configs/vendor/pipa_user_defconfig` to `pipa_defconfig`.
   - Enabled `CONFIG_PSTORE`, `CONFIG_PSTORE_CONSOLE`, `CONFIG_PSTORE_PMSG`, `CONFIG_PSTORE_RAM`, and `CONFIG_PRINTK_TIME` for verbose boot logging and ramoops.

3. **Clocks:**
   - Ported `gcc-kona.c`, `dispcc-kona.c`, `debugcc-kona.c`, `camcc-kona.c`, `gpucc-kona.c`, `npucc-kona.c`, `videocc-kona.c` from 4.19 to 5.4.
   - Added `CONFIG_KONA_GCC` and `CONFIG_KONA_DISPCC` to `drivers/clk/qcom/Kconfig`.

4. **Pinctrl:**
   - Ported `pinctrl-kona.c`.
   - Wired up `CONFIG_PINCTRL_KONA` to `drivers/pinctrl/qcom/Kconfig`.

5. **Display & Panel:**
   - The display panel configuration (`dsi-panel-k81-42-02-0a-video.dtsi`) was included with the device tree repository.
   - Opted *not* to port the Xiaomi-specific MIUI display additions (`dsi_display_mi.c`, `mi_disp_lhbm.c`) as they are MIUI-specific and the 5.4 kernel generic `techpack/display` driver should be able to parse the DTS to drive the display for LineageOS/AOSP.

6. **Touchscreen:**
   - Ported `drivers/input/touchscreen/xiaomi/` from 4.19 to 5.4.
   - Wired up Kconfig and Makefile.

7. **Power/Battery (Charging):**
   - Ported `maxim_pipa`, `qpnp-fg-gen4-pipa`, `qpnp-smb5-pipa`, and `ti` chargers from 4.19 to 5.4.

## Dropped/Stubbed
- `techpack/display/msm/dsi/*mi*.c` (MIUI specific display features).
- Non-essential SoC clocks for initial boot (NPU, Video, GPU are copied but may need further API alignments before use).

## Potential Compilation Issues (To Check)
- **Clock / Pinctrl API changes:** The Qualcomm clock and pinctrl drivers in 5.4 use mostly the same `clk_rcg2` APIs as 4.19, but if struct member names changed (e.g. `clk_hw` wrapping), compilation might fail in `gcc-kona.c`.
- **Power supply drivers:** The `qpnp-smb5-pipa.c` heavily relies on 4.19 Qualcomm SMB5 framework. If `5.4`'s SMB5 framework diverged significantly, compiling `smb5-lib-pipa.c` may throw missing struct members.

