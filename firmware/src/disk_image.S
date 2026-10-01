/* Embeds the game disk image in flash as read-only data.
   The file is found through the -I../game/out flag in platformio.ini.
   After rebuilding the game image, run "pio run -t clean" so this file is
   assembled again. */
    .section .rodata.disk_image, "a", %progbits
    .balign 4
    .global disk_image_start
disk_image_start:
    .incbin "PrinceOfPersia_3.5.hdv"
    .global disk_image_end
disk_image_end:
    .balign 4
