# NuttX UBSan

# Append paths and include directories
VPATH += platforms/common/ubsan
CFLAGS += -Iplatforms/common/ubsan

# Override strict -Werror options
CFLAGS += -Wno-error=unused-parameter
# Avoid infinite recursion in syslog
#CFLAGS += -fno-sanitize=undefined
# Add translation units
SRC += ubsan.c syslog.c
