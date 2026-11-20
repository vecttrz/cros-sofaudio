/*
 * rt1019_reg.h — intentionally empty
 *
 * The Google Marasov / ASUS CX34 amplifier is RTL1019P (ACPI\RTL1019), the
 * GPIO/SDB-only variant equivalent to Linux snd_soc_rt1015p. Its _CRS has
 * no I2C resource and the chip exposes no control register map on this board.
 *
 * This header is kept only so existing project/verification scripts keep
 * working. Rt1019Audio.c controls the amplifier exclusively through
 * IOCTL_GPIO_WRITE_PINS on the GpioIo connection from ACPI.
 *
 * Copyright (c) 2026. Licensed under the MIT License.
 */
#pragma once
