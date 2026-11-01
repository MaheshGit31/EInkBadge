# EInkBadge

## Basic summary
I want to make a badge that use an ESP32 and an E-ink display. I create well.... a fun little badge that I can wear around everywhere! This badge will feature a 2.13" e-paper display, an ESP32-S3 brain, USB-C charging, a Status LED, and a 3D printed case with display window. 

## How it works
 
1. Press the wake button. The badge wakes up and advertises over Bluetooth as `EInkBadge` for 60 seconds.
2. Open the updater page in Chrome or Edge and click Connect.
3. Type a name and subtitle, optionally add an image, and check the preview.
4. Click Send. The page dithers the image to 1 bit, packs it into the display's native 4000-byte buffer, and sends it in 180-byte chunks.
5. The badge refreshes the display and goes back to deep sleep. The image stays on screen with no power.


## Images
<img width="826" height="826" alt="image" src="https://github.com/user-attachments/assets/701285b0-6db3-4fc8-b900-0fd6fe80be5b" />
<img width="826" height="826" alt="image" src="https://github.com/user-attachments/assets/3be92d43-2bc1-4b82-a72d-00b9fa4eab1c" />
