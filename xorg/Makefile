# SPDX-License-Identifier: MIT
CC ?= cc
PKG_CONFIG ?= pkg-config
CFLAGS += -O2 -fPIC -Wall -Wextra -Werror -Wno-unused-parameter \
	$(shell $(PKG_CONFIG) --cflags xorg-server libdrm)
LDLIBS += $(shell $(PKG_CONFIG) --libs libdrm)

.PHONY: all clean
all: tarugx_drv.so

tarugx_drv.so: tarugx_driver.c ../drivers/tarugx/tarugx_drm.h
	$(CC) $(CFLAGS) -shared -I../drivers/tarugx $< $(LDLIBS) -o $@

clean:
	rm -f tarugx_drv.so
