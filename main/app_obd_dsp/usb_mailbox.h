#pragma once
/** Applies and clears the settings the platform wrote over USB (games). Call once at boot, after NVS init. */
void usb_mailbox_apply(void);
