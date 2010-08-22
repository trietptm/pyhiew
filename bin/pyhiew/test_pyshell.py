"""
Sample Hiew based Python statement prompt
"""
import hiew
import traceback

s = hiew.GetString("Enter python statement", 100)
try:
    exec(s, globals())
except Exception, e:
    hiew.Window.FromString(
        'statement error',
        str(e) + "\n" + traceback.format_exc())