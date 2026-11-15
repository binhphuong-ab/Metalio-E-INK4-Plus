#pragma once

// Resume reading after power-off: if a book was open when the reader was
// switched off (from the page or from standby on top of it), the next boot
// reopens that book at its saved position instead of stopping on Home.

/** Called once at the start of power-off: remembers whether a book is open. */
void BookResume_RecordAtPowerOff();

/** Called once the SD card is mounted at boot: reopens the last book if it was open at power-off. */
void BookResume_OnSystemReady();
