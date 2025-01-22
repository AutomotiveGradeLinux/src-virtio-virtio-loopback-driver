# Copyright (C) 2022 Virtual Open Systems SAS
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software Foundation,
# Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
#

LINUX_DIR ?= /lib/modules/$(shell uname -r)/build

virtio_loopback-objs := virtio_loopback_driver.o virtio_loopback_device.o iova_domain.o
obj-m += virtio_loopback.o

ifeq ($(DEBUG), 1)
 ccflags-y += -DDEBUG
endif

CFLAGS := -Wall -Wextra -Werror

all:
	make -C $(LINUX_DIR) M=$(PWD) modules

clean:
	make -C $(LINUX_DIR) M=$(PWD) clean
