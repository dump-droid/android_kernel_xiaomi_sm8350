# Pipa 5.4 boot-port notes

Target tree: `5.4` (Kona / SM8250). The pipa source trees are read-only.

## Ported

1. **Defconfig** — Adapted `pipa-drivers/arch/arm64/configs/vendor/pipa_user_defconfig` to `arch/arm64/configs/vendor/pipa_defconfig`. Retained Android Binder/BinderFS and SELinux, console, ramoops/pstore, UFS, USB gadget FunctionFS, Kona clocks and pinctrl. Used the 5.4 generic `USB_PD_POLICY` symbol because the 4.19 `USB_PD_POLICY_DAGU` option does not exist here.
2. **Kona and pipa DTS** — Reused the generic Kona DTS, GCC, TLMM, SMMU, regulator and support files from `sm8350`; added only pipa board metadata and the PMIC base needed by the boot path. Memory and reserved regions match the pipa 4.19 Kona description.
3. **UFS** — Added the pipa UFS PHY and controller rail configuration from `pipa-dts/qcom/xiaomi-sm8250-common.dtsi`. The `hsg4_synclength` property is omitted because neither kernel tree has a consumer for it.
4. **USB** — Reused the Kona USB controller/PHY description and register bindings from `sm8350/arch/arm64/boot/dts/vendor/qcom/kona-usb.dtsi` and `sm8350/include/dt-bindings/phy/qcom,kona-qmp-usb3.h`. Added pipa USB-C extcon, USB3 PHY sequence and HS PHY parameter data from `pipa-dts/qcom/pipa-sm8250.dtsi`; enabled 5.4 USB PD policy and QPNP PD PHY support. SMB5 charging and fuel gauge remain disabled.
5. **Display panel** — Reused the Kona SDE/DSI and PLL descriptions from the 5.4 tree, with the shared Kona SDE bus and SMMU setup matched to `sm8350/arch/arm64/boot/dts/vendor/qcom/display/kona-sde.dtsi`. Added pipa's dual-link M82 panel timings, DSC and core initialization sequence, reset/TE GPIOs, and panel supply rails from `pipa-dts/qcom/pipa-sm8250.dtsi` and its display panel DTSI. Selected only the vendor SDE/DSI build path for this defconfig and kept DP, writeback and rotator disabled. Removed MI-specific display parameter properties/commands. The panel's external KTZ8866 backlight is a separate next stage.
6. **Backlight** — Adapted the KTZ8866 driver from `pipa-drivers/drivers/video/backlight/ktz8866.c` and its register/brightness data from `pipa-drivers/include/linux/platform_data/ktz8866.h`. Added the pipa SE11 I2C node and exact GPIOs/properties from `pipa-dts/qcom/pipa-sm8250.dtsi`. Preserved the raw backlight class device, 4095 HBM range, brightness mapping and register setup. The 5.4 driver uses per-device state and requests HW_EN/panel-id before registering callbacks; it honors pipa's `backlight-conf-disable` setting and still applies brightness updates.

## Deferred

Touch, charger/fuel gauge, audio, WLAN, Bluetooth, sensors, data techpack, cameras, and accessory support remain for later stages. The secondary USB controller remains disabled as in the pipa overlay.

The pipa 4.19 USB HS PHY also defines `qcom,global-param-override-seq`, selected there through a hardware-country API that is absent from this 5.4 HS PHY driver. That country-specific tuning is not applied; the pipa default HS PHY sequence is applied. SMB5 VBUS/VCONN regulators are not enabled in this USB stage, so host-side power sourcing is not covered.

## Builds and validation

Builds use Clang 20.0.0 from `/home/aryan/tc/bin`, with `ARCH=arm64 LLVM=1` and an out-of-tree `O=out` directory. The configured `vendor/pipa_defconfig` and `olddefconfig` retain the required Android, ramoops, UFS, gadget, PD PHY, Kona and `BACKLIGHT_KTZ8866` symbols. The Kona base DTB and pipa DTBO build, and every external pipa-overlay symbol resolves against the Kona base DTB. After the display and backlight stages, `Image` and `dtbs` build successfully with the vendor SDE/DSI and KTZ8866 objects selected.

The build still reports warnings from existing, untouched 5.4 sources (arm64 assembly debug-section warnings, Binder unused functions, `drivers/thermal/qcom/msm_isense_cdsp.c`, and the static `EXPORT_SYMBOL` warning for `embms_tm_multicast_recv`). The display build exposed an uninitialized DSI display pointer on the non-DSI connector path; its backlight flag write is now guarded by the DSI connector check. The KTZ8866 driver compiles without warnings. No new USB/PD, display or backlight warnings remain.

**UNTESTED on hardware:** boot, UFS partition mounting, panel initialization/backlight, touch/input, USB-C role detection, ADB/fastboot gadget enumeration, and ramoops collection. This port requires tablet boot and USB testing before extending the scope.
