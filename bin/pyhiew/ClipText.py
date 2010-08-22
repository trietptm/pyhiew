"""
Hiew script to copy data from hiew to text.


Usage
========
This script can copy data to various formats (source code representation, plain text)

Copy to:
----------
- Plain text
- C source
- Pascal source


TODO
=====
- Pasting from
  - Plain text

History
=========
- 1.0      - initial version
"""
import hiew

# -----------------------------------------------------------------------
def buf_to_c_array(buf):
    i = 1
    out = []
    for ch in buf:
        out.append('0x%02x, ' % ord(ch))
        if i % CLIP_TEXT_ITEMS_PER_LINE == 0:
            i = 1
            out.append('\n\t')
        else:
            i += 1
    out = ''.join(out).rstrip()[:-1]
    return ('static unsigned char data[%d] = \n{\n\t%s\n};' %
            (len(buf), out))

# -----------------------------------------------------------------------
def buf_to_pascal_array(buf):
    i = 1
    out = []
    for ch in buf:
        out.append('$%02x, ' % ord(ch))
        if i % CLIP_TEXT_ITEMS_PER_LINE == 0:
            i = 1
            out.append('\n\t')
        else:
            i += 1
    out = ''.join(out).rstrip()[:-1]
    return ('const data: array[0..%d] of Byte = \n(\n\t%s\n);' %
            (len(buf)-1, out))

# -----------------------------------------------------------------------
def copy_text_to_clipboard(text):
    try:
        import win32clipboard
    except:
        hiew.Message("Error", "win32clipboard module not installed!")
        return False
    win32clipboard.OpenClipboard()
    win32clipboard.EmptyClipboard()
    win32clipboard.SetClipboardText(text)
    win32clipboard.CloseClipboard()
    return True

# -----------------------------------------------------------------------
def hiew_main():

    try:
        buf = hiew.Data.GetSelData()
        if not buf:
            hiew.Message("Error", "Nothing is selected!")
            return

        global CLIP_TEXT_CHOICE
        m = hiew.Menu()
        m.Create(
            title = "Copy/Paste",
            lines = ["Copy as plain text",    #0
                     "Copy as C array",       #1
                     "Copy as Pascal array",  #2
                     #"Paste plain text"       #3
                     ],
            width = 30)

        n, k = m.Show(CLIP_TEXT_CHOICE)
        if n == 0:
            if copy_text_to_clipboard(buf):
                hiew.Message("Info", "Copied to clipboard as plain text!")
        elif n == 1:
            if copy_text_to_clipboard(buf_to_c_array(buf)):
                hiew.Message("Info", "Copied to clipboard as C array!")
        elif n == 2:
            if copy_text_to_clipboard(buf_to_pascal_array(buf)):
                hiew.Message("Info", "Copied to clipboard as pascal array!")
        elif n == 3:
            # paste from clipboard to file
            pass
        else:
            return
    except Exception, e:
        hiew.MessageBox(str(e), 'Exception')
    CLIP_TEXT_CHOICE = n

# -----------------------------------------------------------------------
try:
    CLIP_TEXT_CHOICE
    CLIP_TEXT_ITEMS_PER_LINE
except:
    CLIP_TEXT_CHOICE = 0
    CLIP_TEXT_ITEMS_PER_LINE = 16

#hiew.MarkBlock(0x4E, 0x6E)
hiew_main()
