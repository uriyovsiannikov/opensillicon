# Source archives

Downloaded over HTTPS from the upstream release sites. Local platform edits
are described in README.md. Original licenses are retained in each tree.

| Component | Version | Archive | SHA-256 |
| --- | --- | --- | --- |
| NetSurf all | 3.11 | https://download.netsurf-browser.org/netsurf/releases/source-full/netsurf-all-3.11.tar.gz | 4dea880ff3c2f698bfd62c982b259340f9abcd7f67e6c8eb2b32c61f71644b7b |
| libcurl | 8.23.0 | https://curl.se/download/curl-8.23.0.tar.xz | 8dadd7d2536a314f982ad05a651b713f426bf7d02cc2c76cffb9796f2218e6c5 |
| Expat | 2.10.0 | https://github.com/libexpat/libexpat/releases/download/R_2_10_0/expat-2.10.0.tar.bz2 | 44e0139015f1c9908d0876b5516235136f16b14932d8f6634a16737a12742f53 |
| gperf (host) | 3.3 | https://ftp.gnu.org/gnu/gperf/gperf-3.3.tar.gz | fd87e0aba7e43ae054837afd6cd4db03a3f2693deb3619085e6ed9d8d9604ad8 |
| xcb-util-0.4.1 |  | https://xorg.freedesktop.org/archive/individual/xcb/xcb-util-0.4.1.tar.xz | 5abe3bbbd8e54f0fa3ec945291b7e8fa8cfd3cccc43718f8758430f94126e512 |
| xcb-util-image-0.4.1 |  | https://xorg.freedesktop.org/archive/individual/xcb/xcb-util-image-0.4.1.tar.xz | ccad8ee5dadb1271fd4727ad14d9bd77a64e505608766c4e98267d9aede40d3d |
| xcb-util-wm-0.4.2 |  | https://xorg.freedesktop.org/archive/individual/xcb/xcb-util-wm-0.4.2.tar.xz | 62c34e21d06264687faea7edbf63632c9f04d55e72114aa4a57bb95e4f888a0b |
| xcb-util-keysyms-0.4.1 |  | https://xorg.freedesktop.org/archive/individual/xcb/xcb-util-keysyms-0.4.1.tar.xz | 7c260a5294412aed429df1da2f8afd3bd07b7cba3fec772fba15a613a6d5c638 |

FreeType, zlib, PNG, JPEG, XCB and Xau reuse the existing Xorg dependencies.
Mbed TLS 3.6.7 and the Mozilla CA bundle live in `../../mbedtls/`; TrueType
fonts and OFL notices live in `../../xorg/fonts/truetype/liberation/`.
