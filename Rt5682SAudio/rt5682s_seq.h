/*
 * rt5682s_seq.h - RTL5682S register sequences (Marasov / CX34 port)
 *
 * Register/bit values and the order of operations follow the Linux kernel
 * driver (sound/soc/codecs/rt5682s.c, rt5682s.h) and the Intel machine driver
 * (sound/soc/intel/boards/sof_rt5682s.c). The ACPI properties were originally
 * taken from google/brya/variants/volmar and are shared with Marasov.
 *
 *   - init:     rt5682s_init_regs() with the coreboot ACPI properties
 *               (jkdet enable, no pull, active low, vref_impedance 2,
 *               micbias 6, debounce 7/7)
 *   - playback: set_dai_fmt (I2S, codec clock consumer), hw_params (32-bit,
 *               DAC OSR 64), sysclk FLL_BLK + set_pll(BCLK 3.072 MHz ->
 *               256*fs) and the DAPM power-up order of the headphone path
 *   - stop:     DAPM power-down in reverse, then system_clock_control()
 *
 * Bit definitions use a NAUL_ prefix inherited from CoolStar's Realtek
 * sequence headers; it is only a macro namespace, not the legacy NAU codec
 * family that the original upstream driver targeted.
 * The prefix also avoids collisions with the partly incorrect definitions in
 * rt5682s_reg.h.
 */
#pragma once

/* ---- register addresses (identical to rt5682s_reg.h / Linux) -------- */
#define NAUL_RESET              0x00
#define NAUL_ENA_CTRL           0x01
#define NAUL_IIC_ADDR_SET       0x02
#define NAUL_CLK_DIVIDER        0x03
#define NAUL_FLL1               0x04
#define NAUL_FLL2               0x05
#define NAUL_FLL3               0x06
#define NAUL_FLL4               0x07
#define NAUL_FLL5               0x08
#define NAUL_FLL6               0x09
#define NAUL_HSD_CTRL           0x0C
#define NAUL_JACK_DET_CTRL      0x0D
#define NAUL_INTERRUPT_MASK     0x0F
#define NAUL_IRQ_STATUS         0x10
#define NAUL_INT_CLR_KEY_STATUS 0x11
#define NAUL_INTERRUPT_DIS_CTRL 0x12
#define NAUL_SAR_CTRL           0x13
#define NAUL_GPIO12_CTRL        0x1A
#define NAUL_I2S_PCM_CTRL1      0x1C
#define NAUL_I2S_PCM_CTRL2      0x1D
#define NAUL_LEFT_TIME_SLOT     0x1E
#define NAUL_ADC_RATE           0x2B
#define NAUL_DAC_CTRL1          0x2C
#define NAUL_MUTE_CTRL          0x31
#define NAUL_HSVOL_CTRL         0x32
#define NAUL_DACL_CTRL          0x33
#define NAUL_DACR_CTRL          0x34
#define NAUL_CLASSG_CTRL        0x50
#define NAUL_I2C_DEVICE_ID      0x58
#define NAUL_SARDOUT_RAM_STATUS 0x59
#define NAUL_FLL2_LOWER         0x5A
#define NAUL_FLL2_UPPER         0x5B
#define NAUL_BIAS_ADJ           0x66
#define NAUL_ANALOG_CONTROL_2   0x6A
#define NAUL_ANALOG_ADC_2       0x72
#define NAUL_RDAC               0x73
#define NAUL_MIC_BIAS           0x74
#define NAUL_BOOST              0x76
#define NAUL_FEPGA              0x77
#define NAUL_POWER_UP_CONTROL   0x7F
#define NAUL_CHARGE_PUMP        0x80
#define NAUL_GENERAL_STATUS     0x82

/* ---- bits ------------------------------------------------------------ */
#define NAUL_ENABLE_DACR        (1u << 10)
#define NAUL_ENABLE_DACL        (1u << 9)
#define NAUL_ENABLE_ADC         (1u << 8)
#define NAUL_ENABLE_ADC_CLK     (1u << 7)
#define NAUL_ENABLE_DAC_CLK     (1u << 6)

#define NAUL_CLK_SRC_MASK       (1u << 15)
#define NAUL_CLK_SRC_VCO        (1u << 15)
#define NAUL_CLK_SRC_MCLK       0
#define NAUL_CLK_ADC_SRC_MASK   (0x3u << 6)
#define NAUL_CLK_DAC_SRC_MASK   (0x3u << 4)
#define NAUL_CLK_MCLK_SRC_MASK  0xFu

#define NAUL_ICTRL_LATCH_MASK   (0x7u << 10)
#define NAUL_FLL_RATIO_MASK     0x7Fu
#define NAUL_GAIN_ERR_MASK      (0xFu << 12)
#define NAUL_FLL_CLK_SRC_MASK   (0x3u << 10)
#define NAUL_FLL_CLK_SRC_BLK    (0x2u << 10)
#define NAUL_FLL_INTEGER_MASK   0x3FFu
#define NAUL_FLL_REF_DIV_MASK   (0x3u << 10)
#define NAUL_FLL_PDB_DAC_EN     (1u << 15)
#define NAUL_FLL_LOOP_FTR_EN    (1u << 14)
#define NAUL_FLL_CLK_SW_MASK    (1u << 13)
#define NAUL_FLL_FTR_SW_MASK    (1u << 12)
#define NAUL_FLL_FTR_SW_ACCU    (1u << 12)
#define NAUL_DCO_EN             (1u << 15)
#define NAUL_SDM_EN             (1u << 14)
#define NAUL_CUTOFF500          (1u << 13)

#define NAUL_HSD_HPL_GND        (1u << 0)   /* "HPOL Pulldown" (inverted widget) */
#define NAUL_HSD_HPR_GND        (1u << 1)
#define NAUL_HSD_MICGND2        (1u << 2)
#define NAUL_HSD_MICGND1        (1u << 3)
#define NAUL_HSD_AUTO_MODE      (1u << 6)

#define NAUL_JACK_POLARITY      (1u << 1)
#define NAUL_JACK_DET_RESTART   (1u << 9)
#define NAUL_JACK_INSERT_DEB_MASK (0x7u << 5)
#define NAUL_JACK_EJECT_DEB_MASK  (0x7u << 2)

#define NAUL_IRQ_PIN_PULLUP     (1u << 14)
#define NAUL_IRQ_PIN_PULL_EN    (1u << 13)
#define NAUL_HEADSET_COMPLETE_IRQ (1u << 10)

#define NAUL_JKDET_PULL_UP      (1u << 11)
#define NAUL_JKDET_PULL_EN      (1u << 9)
#define NAUL_JKDET_OUTPUT_EN    (1u << 8)

#define NAUL_I2S_BP_MASK        (1u << 7)
#define NAUL_I2S_PCMB_MASK      (1u << 6)
#define NAUL_I2S_DL_MASK        (0x3u << 2)
#define NAUL_I2S_DL_32          (0x3u << 2)
#define NAUL_I2S_DF_MASK        0x3u
#define NAUL_I2S_DF_I2S         0x2u
#define NAUL_I2S_MS_MASK        (1u << 3)
#define NAUL_I2S_TRISTATE       (1u << 15)

#define NAUL_DIS_FS_SHORT_DET   (1u << 13)
#define NAUL_ADC_SINC4_EN       (1u << 4)
#define NAUL_ADC_SYNC_DOWN_MASK 0x3u
#define NAUL_ADC_SYNC_DOWN_64   1u
#define NAUL_DAC_CLIP_OFF       (1u << 7)
#define NAUL_DAC_OVERSAMPLE_MASK 0x7u
#define NAUL_DAC_OVERSAMPLE_64  0u

#define NAUL_HP_MUTE            (1u << 15)
#define NAUL_HPL_MUTE           (1u << 13)
#define NAUL_HPR_MUTE           (1u << 12)
#define NAUL_HPL_VOL_SFT        6
#define NAUL_HP_VOL_MASK        0x3Fu

#define NAUL_DAC_CH_SEL_MASK    (1u << 9)
#define NAUL_DAC_CH_SEL_L       0
#define NAUL_DAC_CH_SEL_R       (1u << 9)

#define NAUL_CLASSG_TIMER_MASK  (0x3Fu << 8)
#define NAUL_CLASSG_TIMER_64MS  (0x20u << 8)
#define NAUL_CLASSG_LDAC_EN     (1u << 2)   /* "HP amp L" */
#define NAUL_CLASSG_RDAC_EN     (1u << 1)   /* "HP amp R" */
#define NAUL_CLASSG_EN          (1u << 0)   /* "Class G"  */

#define NAUL_SOFTWARE_ID_MASK   0x3u
#define NAUL_SW_ID_RT5682S      0x0u
#define NAUL_SW_ID_RT5682SC     0x1u
#define NAUL_GPIO2JD1           (1u << 7)

#define NAUL_BIAS_TESTDAC_EN    (0x3u << 8)
#define NAUL_BIAS_VMID          (1u << 6)
#define NAUL_BIAS_VMID_SEL_MASK (0x3u << 4)

#define NAUL_HP_NON_CLASSG_2X   (1u << 12)
#define NAUL_DAC_CAP_MSB        (1u << 1)
#define NAUL_DAC_CAP_LSB        (1u << 0)

#define NAUL_RDAC_EN            (0x3u << 12)   /* "ADACL"/"ADACR" */
#define NAUL_RDAC_CLK_EN        (0x3u << 8)    /* "ADACL/R Clock" */
#define NAUL_RDAC_CLK_DELAY_MASK (0x7u << 4)
#define NAUL_RDAC_VREF_MASK     (0x3u << 2)

#define NAUL_MICBIAS_VOLTAGE_MASK 0x7u
#define NAUL_MICBIAS_POWERUP    (1u << 8)
#define NAUL_MICBIAS_LOWNOISE   (1u << 10)
#define NAUL_MICBIAS_JKR2       (1u << 12)
#define NAUL_MICBIAS_JKSLV      (1u << 14)

#define NAUL_SAR_SAMPLING_TIME_MASK (0x3u << 0)
#define NAUL_SAR_COMPARE_TIME_MASK  (0x3u << 2)
#define NAUL_SAR_RES_SEL_MASK       (0x7u << 4)
#define NAUL_SAR_RES_SEL_70K        (1u << 4)
#define NAUL_SAR_HV_SEL_MASK        (1u << 7)
#define NAUL_SAR_TRACKING_GAIN_MASK (0x7u << 8)
#define NAUL_SAR_INPUT_MASK         (1u << 11)
#define NAUL_SAR_INPUT_JKR2         0
#define NAUL_SAR_INPUT_JKSLV        (1u << 11)
#define NAUL_SAR_ADC_EN             (1u << 12)

#define NAUL_GENERAL_MIC_DETECT_SHIFT 10
#define NAUL_GENERAL_MIC_DETECT_MASK  (0x3u << NAUL_GENERAL_MIC_DETECT_SHIFT)

#define NAUL_PRECHARGE_DIS      (1u << 13)
#define NAUL_GLOBAL_BIAS_EN     (1u << 12)
#define NAUL_DISCHARGE_EN       (1u << 11)
#define NAUL_HP_BOOST_DIS       (1u << 9)      /* "HP Boost Driver" (inverted) */
#define NAUL_HP_BOOST_G_DIS     (1u << 8)
#define NAUL_SHORT_SHUTDOWN_EN  (1u << 6)

#define NAUL_FEPGA_ACDC_MASK    (0x3u << 14)
#define NAUL_FEPGA_ACDC_MICP    (0x1u << 14)
#define NAUL_FEPGA_ACDC_MICN    (0x2u << 14)
#define NAUL_FEPGA_POWERUP      (1u << 14)

#define NAUL_ANALOG_ADC_POWERUP (1u << 6)

#define NAUL_PU_OUT_STAGE1      ((1u << 5) | (1u << 4))  /* R | L */
#define NAUL_PU_OUT_STAGE2      ((1u << 3) | (1u << 2))
#define NAUL_PU_OUT_STAGE3      ((1u << 1) | (1u << 0))

#define NAUL_ADCOUT_DS_MASK     (1u << 12)
#define NAUL_JAMNODCLOW         (1u << 10)
#define NAUL_POWER_DOWN_DACR    (1u << 9)
#define NAUL_POWER_DOWN_DACL    (1u << 8)
#define NAUL_CHARGE_PUMP_EN     (1u << 5)      /* "Charge Pump" */

/* ---- volmar board values (coreboot overridetree.cb) ------------------ */
#define VOLMAR_VREF_IMPEDANCE   2      /* 125 kOhm */
#define VOLMAR_MICBIAS_VOLTAGE  6      /* 2.754 V  */
#define VOLMAR_SAR_VOLTAGE      0      /* VDDA / tracking gain 0 */
#define VOLMAR_SAR_COMPARE_TIME 0      /* 500 ns */
#define VOLMAR_SAR_SAMPLE_TIME  0      /* 2 us */
#define VOLMAR_JACK_INSERT_DEB  7
#define VOLMAR_JACK_EJECT_DEB   7

/*
 * FLL for BCLK 3.072 MHz -> 256 * 48 kHz, computed with Linux
 * rt5682s_calc_fll_param():
 *   fref = 3.072 MHz (pre-scaler /1, clk_ref_div 0)
 *   ratio = 1 (fref >= 512 kHz)
 *   FDCO  = 256*48000*2*5 = 122.88 MHz (largest within 90..124 MHz),
 *           mclk_src scaling 5 -> 0xF
 *   fll_int = 122.88 MHz / 3.072 MHz = 40, fll_frac = 0
 */
#define VOLMAR_FLL_MCLK_SRC     0xFu
#define VOLMAR_FLL_RATIO        0x01u
#define VOLMAR_FLL_INT          40u
#define VOLMAR_FLL_REF_DIV      0u
#define VOLMAR_DAC_CLK_SRC      (2u << 4)   /* osr_dac_sel[OSR64].clk_src = 2 */
#define VOLMAR_ADC_CLK_SRC      (2u << 6)   /* osr_adc_sel[OSR64].clk_src = 2 */

/* jack polling period */
#define NAU_JACK_POLL_MS        300

/* =========================================================================
 * Real RTL5682S register map (v1.3.1)
 *
 * The NAUL_* definitions above describe the legacy NAU-style register map.
 * The silicon fitted to the Marasov / CX34 is a real RTL5682S
 * (vendor 0x10EC, device 0x6749 at registers 0xFE/0xFF).  Everything below
 * is taken from the Linux v6.6 rt5682s driver and CoolStar's working
 * rt5682s driver for the same board family.
 * ========================================================================= */

#define RT_RESET                        0x0000u
#define RT_VENDOR_ID                    0x00FEu
#define RT_VENDOR_ID_VALUE              0x10ECu
#define RT_DEVICE_ID                    0x00FFu
#define RT_DEVICE_ID_VALUE              0x6749u

/* Linux rt5682s_reset() and CoolStar BOOTCODEC() both write 0 to the reset
 * register; 0x8000 is NOT the reset value for this silicon. */
#define RT_RESET_VALUE                  0u

/* Global mute bits used by the STO1 ADC digital volume register. */
#define RT_L_MUTE                       (1u << 15)
#define RT_R_MUTE                       (1u << 7)

#define RT_HP_CTRL_1                    0x0002u
#define RT_HP_CTRL_2                    0x0003u
#define RT_I2C_CTRL                     0x0008u
#define RT_CBJ_CTRL_1                   0x0010u
#define RT_CBJ_CTRL_2                   0x0011u
#define RT_CBJ_CTRL_3                   0x0012u
#define RT_CBJ_CTRL_4                   0x0013u
#define RT_CBJ_CTRL_5                   0x0014u
#define RT_CBJ_CTRL_6                   0x0015u
#define RT_DAC1_DIG_VOL                 0x0019u
#define RT_STO1_ADC_DIG_VOL             0x001Cu
/* Stereo1 ADC mixer (0x26) */
#define RT_M_STO1_ADC_L1                (1u << 15)
#define RT_M_STO1_ADC_R1                (1u << 7)
#define RT_STO1_ADC1L_SRC_ADC           (1u << 13)
#define RT_STO1_ADC1R_SRC_ADC           (1u << 5)
#define RT_STO1_ADC_MIX_ROUTE_MASK \
    (RT_M_STO1_ADC_L1 | RT_STO1_ADC1L_SRC_ADC | \
     RT_M_STO1_ADC_R1 | RT_STO1_ADC1R_SRC_ADC)
#define RT_STO1_ADC_MIXER               0x0026u
#define RT_STO1_DAC_MIXER               0x002Au
#define RT_REC_MIXER                    0x003Cu
/* REC mixer (0x3C): combo-jack boost into RECMIX1L/RECMIX1R. */
#define RT_M_CBJ_RM1_L                  (1u << 7)
#define RT_M_CBJ_RM1_R                  (1u << 6)
#define RT_CBJ_REC_MIX_MASK             (RT_M_CBJ_RM1_L | RT_M_CBJ_RM1_R)
#define RT_CAL_REC                      0x0044u
/* CAL_REC (0x44): RECMIX1L/R power bits. */
#define RT_PWR_RM1_L                    (1u << 0)
#define RT_PWR_RM1_R                    (1u << 8)
#define RT_RECMIX_POWER_MASK            (RT_PWR_RM1_L | RT_PWR_RM1_R)
#define RT_PWR_DIG_1                    0x0061u
#define RT_PWR_DIG_2                    0x0062u
#define RT_PWR_ANLG_1                   0x0063u
#define RT_PWR_ANLG_2                   0x0064u
#define RT_PWR_ANLG_3                   0x0065u
#define RT_CLK_DET                      0x006Bu
#define RT_DMIC_CTRL_1                  0x006Eu
#define RT_I2S1_SDP                     0x0070u
#define RT_I2S2_SDP                     0x0071u
#define RT_ADDA_CLK_1                   0x0073u
#define RT_TDM_CTRL                     0x0079u
#define RT_TDM_ADDA_CTRL_1              0x007Au
#define RT_TDM_ADDA_CTRL_2              0x007Bu
#define RT_TDM_TCON_CTRL_1              0x007Eu
#define RT_GLB_CLK                      0x0080u
#define RT_PLL_TRACK_1                  0x0083u
#define RT_PLL_TRACK_2                  0x0084u
#define RT_PLL_TRACK_3                  0x0085u
#define RT_FILTER_CLK_DIV_MASK          (0xFu << 8)
#define RT_FILTER_CLK_DIV_SFT           8
#define RT_PLL_CTRL_1                   0x0098u
#define RT_PLL_CTRL_2                   0x0099u
#define RT_PLL_CTRL_3                   0x009Au
#define RT_PLL_CTRL_4                   0x009Bu
#define RT_PLL_CTRL_6                   0x009Du
#define RT_PLL_CTRL_7                   0x009Eu
#define RT_DEPOP_1                      0x008Eu
#define RT_PWR_MIXER                    0x0066u
#define RT_MICBIAS_2                    0x0094u
#define RT_RC_CLK_CTRL                  0x009Fu
#define RT_I2S2_M_CLK_CTRL_1            0x00A0u
#define RT_IRQ_CTRL_2                   0x00B7u
#define RT_IRQ_CTRL_3                   0x00B8u
#define RT_INT_ST_1                     0x00BEu
#define RT_GPIO_CTRL_1                  0x00C0u
#define RT_GPIO_ST                      0x00C2u
#define RT_IL_CMD_1                     0x00DBu
#define RT_4BTN_IL_CMD_1                0x00E2u
#define RT_4BTN_IL_CMD_2                0x00E3u
#define RT_4BTN_IL_CMD_4                0x00E5u
#define RT_4BTN_IL_CMD_5                0x00E6u
#define RT_4BTN_IL_CMD_6                0x00E7u
#define RT_4BTN_IL_CMD_7                0x00E8u
#define RT_AJD1_CTRL                    0x00F0u
#define RT_BIAS_CUR_CTRL_11             0x0610u
#define RT_BIAS_CUR_CTRL_12             0x0611u
#define RT_CHOP_DAC_2                   0x013Au
#define RT_CHOP_ADC                     0x013Bu
#define RT_CKGEN_ADC1                   (1u << 12)
#define RT_DIG_IN_CTRL_1                0x0132u
#define RT_VREF_REC_OP_FB_CAP_CTRL_1    0x0117u
#define RT_VREF_REC_OP_FB_CAP_CTRL_2    0x0118u
#define RT_HP_CALIB_CTRL_9              0x01E6u
#define RT_HP_CALIB_CTRL_1              0x01DEu
#define RT_HP_CALIB_CTRL_2              0x01DFu
#define RT_HP_CALIB_ST_1                0x01EAu
#define RT_HP_LOGIC_CTRL_2              0x01DBu
#define RT_HP_AMP_DET_CTRL_1            0x00D0u
#define RT_HP_CHARGE_PUMP_2             0x0091u
#define RT_SAR_IL_CMD_1                 0x0210u
#define RT_SAR_IL_CMD_2                 0x0211u
#define RT_SAR_IL_CMD_3                 0x0212u
#define RT_SAR_IL_CMD_13                0x021Cu

/* AJD1_CTRL (0xF0) */
#define RT_JDH_RS_MASK                  (1u << 4)
#define RT_JACK_INSERTED                0u
#define RT_JACK_REMOVED                 (1u << 4)

/* CBJ_CTRL_1 (0x10) */
#define RT_EMB_JD_EN                    (1u << 15)
#define RT_DET_TYPE                     (1u << 12)
#define RT_POL_FAST_OFF_HIGH            (1u << 8)
#define RT_FAST_OFF_MASK                (1u << 7)
#define RT_FAST_OFF_EN                  (1u << 7)
#define RT_FAST_OFF_DIS                 0u
#define RT_TRIG_JD_MASK                 (1u << 3)
#define RT_TRIG_JD_HIGH                 (1u << 3)
#define RT_TRIG_JD_LOW                  0u
#define RT_MIC_CAP_MASK                 (1u << 1)
#define RT_MIC_CAP_HS                   (1u << 1)

/* CBJ_CTRL_2 (0x11) */
#define RT_EXT_JD_SRC_MASK              (0x7u << 4)
#define RT_EXT_JD_SRC_MANUAL            (0x4u << 4)
#define RT_JACK_TYPE_MASK               0x3u

/* CBJ_CTRL_3 (0x12) */
#define RT_CBJ_IN_BUF_MASK              (1u << 7)
#define RT_CBJ_IN_BUF_EN                (1u << 7)
#define RT_CBJ_IN_BUF_DIS               0u

/* CBJ_CTRL_4 (0x13) */
#define RT_JD2_DET_EN                   (1u << 0)

/* CBJ_CTRL_5 (0x14) */
#define RT_JD_FAST_OFF_SRC_MASK         (0x7u << 8)
#define RT_JD_FAST_OFF_SRC_JDH          (0x6u << 8)

/* SAR_IL_CMD_2 (0x211) / SAR_IL_CMD_13 (0x21C) */
#define RT_SAR_ADC_PSV_MASK             (1u << 4)
#define RT_SAR_ADC_PSV_ENTRY            (1u << 4)
#define RT_SAR_SOUR_MASK                0x3Fu
#define RT_SAR_SOUR_TYPE                0u
#define RT_SAR_SOUR_BTN                 0x3Fu

/* DAC1_DIG_VOL (0x19).  Linux drives bits 9..15 / 1..7 as a 7-bit volume
 * (127 * 0.75 dB, 0xFE ~= 0 dB); bit 8 / bit 0 is the per-channel mute. */
#define RT_DAC_L1_VOL_MASK              (0xFFu << 8)
#define RT_DAC_R1_VOL_MASK              0xFFu
#define RT_DAC_VOL_0DB                  0xFEu
#define RT_DAC_MUTE_MASK                0x0101u
#define RT_DAC_VOL_MUTE                 0x0101u

/* HP_CTRL_1 (0x02): 7-bit signed analog gain per channel.  The Alder Lake
 * initialization from the working CoolStar driver uses 0x8080 (0 dB).  Bits
 * 15/7 are the gain sign bits here, not a software mute; Windows mute is
 * applied in DAC1_DIG_VOL above. */
#define RT_HP_CTRL_1_0DB                0x8080u
#define RT_HP_L_VOL_MASK                (0x7Fu << 8)
#define RT_HP_R_VOL_MASK                0x7Fu

/* HP_CTRL_2 (0x03) */
#define RT_HPO_L_PATH_MASK              (1u << 14)
#define RT_HPO_R_PATH_MASK              (1u << 13)
#define RT_HPO_SEL_IP_EN_SW             1u
#define RT_HPO_IP_EN_GATING             1u

/* STO1_DAC_MIXER (0x2A) */
#define RT_M_DAC_L1_STO_L_MASK          (1u << 15)
#define RT_M_DAC_R1_STO_R_MASK          (1u << 5)

/* PWR_DIG_1 (0x61) */
#define RT_PWR_I2S1                     (1u << 15)
#define RT_PWR_DAC_L1                   (1u << 11)
#define RT_PWR_DAC_R1                   (1u << 10)
#define RT_PWR_ADC_L1                   (1u << 4)
#define RT_PWR_ADC_R1                   (1u << 3)

/* PWR_DIG_2 (0x62) */
#define RT_PWR_ADC_S1F                  (1u << 15)
#define RT_PWR_DAC_S1F                  (1u << 10)
#define RT_DLDO_I_LIMIT_MASK            (1u << 7)
#define RT_DLDO_I_LIMIT_DIS             0u

/* PWR_MIXER (0x66) */
#define RT_PWR_STO1_DAC_L               (1u << 5)
#define RT_PWR_STO1_DAC_R               (1u << 4)

/* PWR_ANLG_1 (0x63) */
#define RT_PWR_VREF1                    (1u << 15)
#define RT_PWR_FV1                      (1u << 14)
#define RT_PWR_VREF2                    (1u << 13)
#define RT_PWR_FV2                      (1u << 12)
#define RT_PWR_MB                       (1u << 9)
#define RT_PWR_BG                       (1u << 7)

/* PWR_ANLG_2 (0x64) */
#define RT_PWR_MCLK0_WD                 (1u << 15)
#define RT_PWR_MCLK1_WD                 (1u << 14)
#define RT_RST_MCLK0                    (1u << 13)
#define RT_RST_MCLK1                    (1u << 12)
#define RT_PWR_MB1                      (1u << 11)
#define RT_PWR_MB2                      (1u << 10)
#define RT_PWR_JD_MASK                  1u
#define RT_PWR_JD_ENABLE                1u
#define RT_PWR_JD_DISABLE               0u

/* PWR_ANLG_3 (0x65) */
#define RT_PWR_LDO_PLLB                 (1u << 14)
#define RT_PWR_BIAS_PLLB                (1u << 12)
#define RT_PWR_CBJ                      (1u << 9)
#define RT_RSTB_PLLB                    (1u << 7)
#define RT_PWR_LDO_MB1                  (1u << 1)
#define RT_PWR_LDO_MB2                  (1u << 2)
#define RT_PWR_PLLB                     (1u << 5)
#define RT_PWR_BGLDO                    1u
#define RT_PLLB_POWER_MASK              (RT_PWR_LDO_PLLB | RT_PWR_BIAS_PLLB | \
                                         RT_PWR_PLLB | RT_RSTB_PLLB)

/* I2S1_SDP (0x70) */
#define RT_SEL_ADCDAT_MASK              (1u << 15)
#define RT_SEL_ADCDAT_OUT               0u
#define RT_I2S1_MONO_MASK               (1u << 7)
#define RT_I2S1_TX_CHL_MASK             (0x7u << 12)
#define RT_I2S1_RX_CHL_MASK             (0x7u << 8)
#define RT_I2S1_TX_CHL_32               (0x3u << 12)
#define RT_I2S1_RX_CHL_32               (0x3u << 8)
#define RT_I2S1_DL_MASK                 (0x7u << 4)
#define RT_I2S1_DL_32                   (0x3u << 4)
#define RT_I2S_DF_MASK                  0x7u
#define RT_I2S_DF_I2S                   0u

/* I2S2_SDP (0x71) */
#define RT_I2S2_MS_MASK                 (1u << 15)

/* ADDA_CLK_1 (0x73) */
#define RT_ADC_OSR_MASK                 (0xFu << 12)
#define RT_DAC_OSR_MASK                 0xFu
#define RT_I2S_M_CLK_SRC_MASK           (0x7u << 4)
#define RT_I2S_M_CLK_SRC_SFT            4

/* TDM_CTRL (0x79) */
#define RT_TDM_TX_CH_MASK               (0x3u << 12)
#define RT_TDM_RX_CH_MASK               (0x3u << 8)
#define RT_TDM_ADC_DL_MASK              0x3u

/* TDM_ADDA_CTRL_1 (0x7A) / TDM_ADDA_CTRL_2 (0x7B).
 * The working Alder Lake values are 0x8000 / 0x0080: CTRL_1 uses bit 15,
 * while the TDM enable bit used by Linux set_tdm_slot() is bit 7 of CTRL_2. */
#define RT_TDM_CTRL1_IF_EN              (1u << 15)
#define RT_TDM_EN                       (1u << 7)

/* TDM_TCON_CTRL_1 (0x7E) */
#define RT_TDM_DF_MASK                  (0x7u << 11)
#define RT_TDM_DF_I2S                   0u
#define RT_TDM_BCLK_MS1_MASK            (0x3u << 8)
#define RT_TDM_CL_MASK                  (0x3u << 4)
#define RT_TDM_CL_32                    (0x3u << 4)
#define RT_TDM_MS_MASK                  1u
#define RT_TDM_MS_S                     0u

/* GLB_CLK (0x80) */
#define RT_SCLK_SRC_MASK                (0x7u << 13)
#define RT_SCLK_SRC_SFT                 13
#define RT_CLK_SRC_MCLK                 0u
#define RT_CLK_SRC_PLL2                 2u
#define RT_PLL_SRC_MASK                 (0x3u << 8)
#define RT_PLL_SRC_MCLK                 0u

/* DEPOP_1 (0x8E) */
#define RT_DEPOP1_DAC_REF_MASK          0xFu
#define RT_DEPOP1_DAC_REF_DIS           0u
#define RT_OUT_HP_L_EN                  (1u << 6)
#define RT_OUT_HP_R_EN                  (1u << 5)
#define RT_LDO_PUMP_EN                  (1u << 4)
#define RT_PUMP_EN                      (1u << 3)
#define RT_CAPLESS_L_EN                 (1u << 1)
#define RT_CAPLESS_R_EN                 1u

/* MICBIAS_2 (0x94) */
#define RT_PWR_CLK25M_MASK              (1u << 9)
#define RT_PWR_CLK1M_MASK               (1u << 8)
#define RT_PWR_CLK25M_PD                0u
#define RT_PWR_CLK1M_PU                 (1u << 8)

/* I2S2_M_CLK_CTRL_1 (0xA0) */
#define RT_I2S2_M_CLK_SRC_MASK          (0x7u << 4)
#define RT_I2S2_M_CLK_SRC_SFT           4

/* IRQ_CTRL_2 (0xB7) */
#define RT_JD1_EN_MASK                  (1u << 15)
#define RT_JD1_EN                       (1u << 15)
#define RT_JD1_POL_MASK                 (1u << 13)
#define RT_JD1_POL_NOR                  0u

/* IRQ_CTRL_3 (0xB8) */
#define RT_IL_IRQ_MASK                  (1u << 7)
#define RT_IL_IRQ_EN                    (1u << 7)
#define RT_IL_IRQ_DIS                   0u

/* GPIO_CTRL_1 (0xC0) */
#define RT_GP1_PIN_MASK                 (0x3u << 14)
#define RT_GP1_PIN_IRQ                  (1u << 14)

/* HP_AMP_DET_CTRL_1 (0xD0) */
#define RT_CP_SW_SIZE_MASK              (0x7u << 4)
#define RT_CP_SW_SIZE_L                 (0x4u << 4)
#define RT_CP_SW_SIZE_M                 (0x2u << 4)
#define RT_CP_SW_SIZE_S                 (0x1u << 4)

/* 4BTN_IL_CMD_2 (0xE3) */
#define RT_4BTN_IL_MASK                 (1u << 15)
#define RT_4BTN_IL_EN                   (1u << 15)
#define RT_4BTN_IL_DIS                  0u
#define RT_4BTN_IL_RST_MASK             (1u << 14)
#define RT_4BTN_IL_NOR                  (1u << 14)

/* 4BTN_IL_CMD_4..7 */
#define RT_4BTN_IL_HOLD_WIN_MASK        (0x7Fu << 8)
#define RT_4BTN_IL_HOLD_WIN_SFT         8
#define RT_4BTN_IL_CLICK_WIN_MASK       0x7Fu

/* HP_CHARGE_PUMP_2 (0x91) */
#define RT_PWR_CP                       (1u << 15)
#define RT_PWR_HP_L                     (1u << 14)
#define RT_PWR_HP_R                     (1u << 13)
#define RT_OSW_L_MASK                   (1u << 11)
#define RT_OSW_R_MASK                   (1u << 10)
#define RT_OSW_L_DIS                    0u
#define RT_OSW_R_DIS                    0u
#define RT_OSW_L_EN                     (1u << 11)
#define RT_OSW_R_EN                     (1u << 10)
#define RT_PM_HP_MASK                   (0x3u << 8)
#define RT_PM_HP_HV                     (0x2u << 8)

/* HP_LOGIC_CTRL_2 (0x1DB) */
#define RT_HP_SIG_SRC_MASK              0x3u
#define RT_HP_SIG_SRC_1BIT_CTL          0x3u

/* SAR_IL_CMD_1 (0x210) */
#define RT_SAR_BUTDET_MASK              (1u << 15)
#define RT_SAR_BUTDET_EN                (1u << 15)
#define RT_SAR_BUTDET_DIS               0u
#define RT_SAR_BUTDET_POW_MASK          (1u << 14)
#define RT_SAR_BUTDET_POW_SAV           (1u << 14)
#define RT_SAR_BUTDET_POW_NORM          0u
#define RT_SAR_POW_MASK                 (1u << 12)
#define RT_SAR_POW_EN                   (1u << 12)
#define RT_SAR_SEL_MB1_2_MASK           (0x3u << 8)
#define RT_SAR_SEL_MB1_2_SFT            8
#define RT_SAR_SEL_MB1_2_CTL_MASK       (1u << 5)
#define RT_SAR_SEL_MB1_2_AUTO           (1u << 5)
#define RT_SAR_SEL_MB1_2_MANU           0u

/* RC_CLK_CTRL (0x9F) */
#define RT_POW_IRQ                      (1u << 15)
#define RT_POW_JDH                      (1u << 14)

/* PLL Ctrl */
#define RT_PLLA_N_MASK                  0x1FFu
#define RT_PLLA_M_MASK                  (0x1Fu << 8)
#define RT_PLLA_M_SFT                   8
#define RT_PLLA_K_MASK                  0x1Fu
#define RT_PLLB_N_MASK                  0x3FFu
#define RT_PLLB_M_MASK                  (0x1Fu << 8)
#define RT_PLLB_M_SFT                   8
#define RT_PLLB_K_MASK                  0x1Fu
#define RT_PLLB_SRC_MASK                1u
#define RT_PLLB_SRC_DFIN                1u
#define RT_PLLB_SEL_PS_MASK             (1u << 13)
#define RT_PLLB_BYP_PS_MASK             (1u << 12)
#define RT_PLLB_M_BP_MASK               (1u << 11)
#define RT_PLLB_K_BP_MASK               (1u << 10)

/* Jack detect type classification (from CBJ_CTRL_2[1:0]) */
#define RT_JACK_TYPE_HEADPHONE          0u
#define RT_JACK_TYPE_OMTP               1u
#define RT_JACK_TYPE_CTIA               2u

/* CBJ_CTRL_1 trigger and fast-off controls used by headset detection. */
#define RT_CBJ_VREF_POW_MASK            (1u << 6)
#define RT_CBJ_VREF_POW_REG             (1u << 6)
#define RT_MB1_PATH_MASK                (1u << 5)
#define RT_CTRL_MB1_REG                 (1u << 5)
#define RT_CTRL_MB1_FSM                 0u
#define RT_MB2_PATH_MASK                (1u << 4)
#define RT_CTRL_MB2_REG                 (1u << 4)
#define RT_CTRL_MB2_FSM                 0u
