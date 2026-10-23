/* SPDX-License-Identifier: GPL-3.0-only */
/* Addresses only, verified against EP147 2.00. No firmware bytes. */
#define HEIGHT_FW "ep147-9e9454e357389ece"
#ifndef HEIGHT_TEST
#define FW_VIEW ((uintptr_t)0x2d61ae0u)
#define FW_RELAYOUT ((uintptr_t)0x1b3fa60u)
#define FW_BOUNDS ((uintptr_t)0x260c480u)
#define FW_BOX ((uintptr_t)0x2ee92d0u)
#define FW_FIELD ((uintptr_t)0x2eee650u)
#define FW_OWNER ((uintptr_t)0x2ee9078u)
#define FW_BLUE ((uintptr_t)0x2ee0c08u)
#define FW_RGB ((uintptr_t)0x2ee1278u)
#define FW_BAND ((uintptr_t)0x2ee18e8u)
#define FW_PAINT ((uintptr_t)0x1b3aaa0u)
/* Component::paintComponentAndChildren(this, Graphics&). Calls paint slot 26,
 * then child traversal, then paintOverChildren slot 27. Not the core-owned
 * WaveformView::paint function; native vtables are in the RX .rodata segment. */
#define FW_COMPONENT_PAINT ((uintptr_t)0x25d4d30u)
#define FW_HEIGHT ((uintptr_t)0x1cf7b40u)
#endif
