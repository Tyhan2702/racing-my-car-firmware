#pragma once
/** Applies and clears the settings the platform wrote over USB (games, menu). Call once at boot, after NVS init. */
void usb_mailbox_apply(void);
