/* SPDX-License-Identifier: GPL-3.0-only */
/* Independently verified EP147 2.00 addresses. No proprietary code/assets. */
#define PV_FW "ep147-9e9454e357389ece"
#ifndef PV_TEST
#define FW_VIEW ((uintptr_t)0x2d61ae0)
#define FW_FIELD ((uintptr_t)0x2eee650)
#define FW_BOX ((uintptr_t)0x2ee92d0)
#define FW_OWNER ((uintptr_t)0x2ee9078)
#define FW_PREVIEW ((uintptr_t)0x2f1a3b8)
#define FW_PREVIEW_FAMILY ((uintptr_t)0x2f1cdc8)
#define FW_PI2 ((uintptr_t)0x30ef370)
#define FW_PI4 ((uintptr_t)0x30f0328)
#define FW_SW2 ((uintptr_t)0x3093008)
#define FW_SW4 ((uintptr_t)0x3098688)
#define FW_FX_BODY ((uintptr_t)0x312be20)
#define FW_FX_CONTAINER ((uintptr_t)0x312d190)
#define FW_INFO ((uintptr_t)0x3084a68)
/* paintEntireComponent, distinct from the dispatcher claimed by height mod. */
#define FW_PAINT_ENTIRE ((uintptr_t)0x25d4a80)
#define FW_PAINT_CHILDREN ((uintptr_t)0x25d4d30)
/* Entry starts with a visibility test and TBZ. Keep that test native, and hook
 * at its first stack-frame instruction. x0/x1 and SP are unchanged there;
 * the scratch w2 from the visibility test is overwritten before it is used. */
#define FW_COMPONENT_ENTRY ((uintptr_t)0x25f03d0)
#define FW_COMPONENT_AT ((uintptr_t)0x25f03d8)
#define FW_COMPONENT_FNV UINT64_C(0x0769253d493e9871)
#define FW_RELAYOUT ((uintptr_t)0x1b3fa60)
#define FW_REPAINT ((uintptr_t)0x25f31e0)
#define FW_G_SAVE ((uintptr_t)0x251c390)
#define FW_G_RESTORE ((uintptr_t)0x251c3e0)
#define FW_G_CLIP ((uintptr_t)0x251c100)
#define FW_G_TRANSFORM ((uintptr_t)0x251c500)
#define FW_G_COLOUR ((uintptr_t)0x251c5e0)
#define FW_G_FILL ((uintptr_t)0x251cb60)
#define FW_RO_BEGIN ((uintptr_t)0x28fa0c0)
#define FW_RO_END ((uintptr_t)0x3f5f714)
#endif
