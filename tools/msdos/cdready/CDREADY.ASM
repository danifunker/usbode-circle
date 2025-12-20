; cdready - Check whether a CD-ROM drive has readable media
; Copyright (C) 2025  Petteri Valkonen <petteri.valkonen@iki.fi>
;
; This program is free software: you can redistribute it and/or modify
; it under the terms of the GNU General Public License as published by
; the Free Software Foundation, either version 3 of the License, or
; (at your option) any later version.
;
; This program is distributed in the hope that it will be useful,
; but WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
; GNU General Public License for more details.
;
; You should have received a copy of the GNU General Public License
; along with this program.  If not, see <https://www.gnu.org/licenses/>.
;
; The MSCDEX driver and CD-ROM checks were gleaned from the CD-ROM
; programming FAQ at <https://www.flipcode.com/documents/cdromfaq.txt>.

org     0x100           ; COM file format

;==============================================================================
; MAIN CODE
;==============================================================================

; Get drive letter from command line
        mov     si, 0x80        ; Point to command line length
        mov     cl, [si]        ; Get length
        or      cl, cl          ; Check if empty
        jz      usage           ; If no parameters, show usage

        inc     si              ; Skip length byte
        mov     al, [si+1]      ; Get first character (skip space)

        ; Convert drive letter to drive number (A = 0, B = 1 etc.)
        and     al, 0xDF        ; Convert to uppercase
        sub     al, 'A'         ; Convert to drive number
        jc      usage           ; Invalid if < 'A'
        cmp     al, 25          ; Check if > 'Z'
        ja      usage

        mov     [drive_num], al ; Save drive number

        ; Check if MSCDEX is installed
        mov     ax, 0x1500      ; MSCDEX installation check
        xor     bx, bx          ; Clear BX
        int     0x2F
        cmp     bx, 0           ; BX = 0 means not installed
        je      no_mscdex       ; MSCDEX not installed

        ; Check if this drive is a CD-ROM drive
        mov     ax, 0x150B      ; Check if drive is CD-ROM
        mov     cl, [drive_num] ; CL = drive number (0-based)
        int     0x2F
        cmp     bx, 0xADAD      ; Magic return value if it's a CD-ROM
        jne     not_cdrom       ; Not a CD-ROM drive
        cmp     ax, 0           ; AX != 0 means it's a CD-ROM
        je      not_cdrom

        ; Save old critical error handler
        mov     ax, 0x3524      ; Get INT 24h vector
        int     0x21
        mov     [old_int24], bx
        mov     [old_int24+2], es

        ; Install our critical error handler
        mov     ax, 0x2524      ; Set INT 24h vector
        mov     dx, crit_err_handler
        int     0x21

        ; Build path string with actual drive letter
        mov     al, [drive_num]
        add     al, 'A'         ; Convert to letter
        mov     [drive_path], al ; Set drive letter

        ; Try to access root directory via DOS
        mov     ax, 0x4E00      ; Find first file
        mov     cx, 0x0010      ; Directory attribute
        mov     dx, drive_path  ; Point to constructed path
        int     0x21
        pushf                   ; Save flags (carry indicates error)

        ; Restore old critical error handler
        push    ds
        mov     ax, 0x2524      ; Set INT 24h vector
        lds     dx, [old_int24]
        int     0x21
        pop     ds

        popf                    ; Restore flags
        jc      not_ready       ; If error, drive not ready

ready:
        mov     dx, ready_msg
        mov     ah, 0x09        ; Print string
        int     0x21
        mov     al, 0           ; No error
        jmp     exit

no_mscdex:
        mov     dx, no_mscdex_msg
        mov     ah, 0x09        ; Print string
        int     0x21
        mov     al, 2           ; MSCDEX not installed
        jmp     exit

not_ready:
        mov     dx, not_ready_msg
        mov     ah, 0x09        ; Print string
        int     0x21
        mov     al, 3           ; Media not ready
        jmp     exit

not_cdrom:
        mov     dx, not_cdrom_msg
        mov     ah, 0x09        ; Print string
        int     0x21
        mov     al, 4           ; Not a CD-ROM drive
        jmp     exit

usage:
        mov     dx, usage_msg
        mov     ah, 0x09        ; Print string
        int     0x21
        mov     al, 1           ; Bad usage

exit:
        mov     ah, 0x4C        ; Terminate with return code
        int     0x21

;==============================================================================
; INTERRUPT HANDLERS
;==============================================================================

; Critical error handler - always return fail
crit_err_handler:
        mov     al, 3           ; Fail
        iret

;==============================================================================
; DATA SECTION
;==============================================================================

usage_msg:
        db      'Usage: cdready <drive letter>', 0x0D, 0x0A
        db      'Returns: 0 = Ready, 1 = Bad usage, 2 = No MSCDEX,', 0x0D, 0x0A
        db      '         3 = Not ready, 4 = Not a CD-ROM', 0x0D, 0x0A, '$'

no_mscdex_msg:
        db      'Error: MSCDEX not installed', 0x0D, 0x0A, '$'

not_cdrom_msg:
        db      'Error: Not a CD-ROM drive', 0x0D, 0x0A, '$'

not_ready_msg:
        db      'Error: Media not ready', 0x0D, 0x0A, '$'

ready_msg:
        db      'Media ready', 0x0D, 0x0A, '$'

drive_num:
        db      0

drive_path:
        db      '?:\*.*', 0

old_int24:
        dd      0
