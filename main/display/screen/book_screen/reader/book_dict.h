#pragma once

// Dictionary mode in the reader (same flow as CrossMux on the Metalio E-Ink 4):
//   hold Home   -> enter: the word nearest the page centre is highlighted and looked up
//   tap a word  -> highlight and look it up; tap the card to page the definition
//   prev / next -> previous / next word
//   press Home  -> leave

/** True while dictionary mode is shown (any thread). */
bool BookDict_Active();
/** Enter dictionary mode on the current page (LVGL thread; lv_async callback signature). */
void BookDict_EnterAsync(void* user_data);
/** Leave dictionary mode (LVGL thread). Safe to call when not active. */
void BookDict_Close();
/** Route a cover key while active; posts the work to the LVGL thread. Returns true if consumed. */
bool BookDict_OnKey(const char* key_name);
