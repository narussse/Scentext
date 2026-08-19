/* generated HAL source file - do not edit */
#include "hal_data.h"

dmac_instance_ctrl_t g_transfer0_ctrl;
transfer_info_t g_transfer0_info = { .transfer_settings_word_b.dest_addr_mode =
		TRANSFER_ADDR_MODE_INCREMENTED, .transfer_settings_word_b.repeat_area =
		TRANSFER_REPEAT_AREA_SOURCE, .transfer_settings_word_b.irq =
		TRANSFER_IRQ_END, .transfer_settings_word_b.chain_mode =
		TRANSFER_CHAIN_MODE_DISABLED, .transfer_settings_word_b.src_addr_mode =
		TRANSFER_ADDR_MODE_FIXED, .transfer_settings_word_b.size =
		TRANSFER_SIZE_4_BYTE, .transfer_settings_word_b.mode =
		TRANSFER_MODE_BLOCK, .p_dest = (void*) NULL,
		.p_src = (void const*) NULL, .num_blocks = 0, .length = 0, };
const dmac_extended_cfg_t g_transfer0_extend = { .offset = 1, .src_buffer_size =
		1,
#if defined(VECTOR_NUMBER_DMAC0_INT)
    .irq                 = VECTOR_NUMBER_DMAC0_INT,
#else
		.irq = FSP_INVALID_VECTOR,
#endif
		.ipl = (12), .channel = 0, .p_callback = pdm_rxi_dmac_isr, .p_context =
				&g_pdm0_ctrl, .activation_source = ELC_EVENT_PDM_DAT2, };
const transfer_cfg_t g_transfer0_cfg = { .p_info = &g_transfer0_info,
		.p_extend = &g_transfer0_extend, };
/* Instance structure to use this module. */
const transfer_instance_t g_transfer0 = { .p_ctrl = &g_transfer0_ctrl, .p_cfg =
		&g_transfer0_cfg, .p_api = &g_transfer_on_dmac };
pdm_instance_ctrl_t g_pdm0_ctrl;

/** PDM instance configuration */
const pdm_extended_cfg_t g_pdm0_cfg_extend = { .clock_div = PDM_CLOCK_DIV_2,

/** Function Settings. */
.short_circuit_detection_enable = PDM_SHORT_CIRCUIT_DISABLED,
		.over_voltage_lower_limit_detection_enable =
				PDM_OVERVOLTAGE_LOWER_LIMIT_DISABLED,
		.over_voltage_upper_limit_detection_enable =
				PDM_OVERVOLTAGE_UPPER_LIMIT_DISABLED,
		.buffer_overwrite_detection_enable =
				PDM_BUFFER_OVERWRITE_DETECTION_ENABLED,

		/** Filter Settings. */
		.moving_average_mode = PDM_MOVING_AVERAGE_MODE_1_ORDER,
		.low_pass_filter_shift = PDM_LPF_RIGHT_SHIFT_0,
		.compensation_filter_shift = PDM_COMPENSATION_FILTER_RIGHT_SHIFT_0,
		.high_pass_filter_shift = PDM_HPF_RIGHT_SHIFT_0, .sinc_filter_mode =
				PDM_SINC_FILTER_MODE_4,
		.sincrng = PDM2_CALCULATED_SINCRNG_VALUE, .sincdec =
				PDM2_CALCULATED_SINCDEC_VALUE, .hpf_coefficient_s0 = 0x3F61,
		.hpf_coefficient_k1 = 0x3EC1, .hpf_coefficient_h = { 0x4000, 0xC000 },
		.compensation_filter_coefficient_h = { 0x1FE8, 0x0039, 0x003C, 0x1E56,
				0x01DC, 0x06E1, 0x01DC, 0x1E56, 0x003C, 0x0039, 0x1FE8 },
		.lpf_coefficient_h0 = 0x0400, .lpf_coefficient_h1 = { 0x1FF8, 0x000A,
				0x1FF0, 0x0018, 0x1FDC, 0x0034, 0x1FB3, 0x0076, 0x1F2E, 0x0289,
				0x0289, 0x1F2E, 0x0076, 0x1FB3, 0x0034, 0x1FDC, 0x0018, 0x1FF0,
				0x000A, 0x1FF8 },

		/** Data Threshold. */
		.interrupt_threshold = PDM_INTERRUPT_THRESHOLD_16,

		/** Short-Circuit Detection. */
		.short_circuit_count_h = 0x0, .short_circuit_count_l = 0x0,

		/** Overvoltage Detection. */
		.overvoltage_detection_lower_limit = 0x0,
		.overvoltage_detection_upper_limit = 0x0,

};

/** PDM interface configuration */
const pdm_cfg_t g_pdm0_cfg = { .unit = 0, .channel = 2, .pcm_width =
		PDM_PCM_WIDTH_20_BITS_0_18, .pcm_edge = PDM_INPUT_DATA_EDGE_RISE,

#define RA_NOT_DEFINED (1)
#if (RA_NOT_DEFINED == g_transfer0)
                .p_transfer_rx                         = NULL,
#else
		.p_transfer_rx = &g_transfer0,
#endif
#undef RA_NOT_DEFINED
		.p_callback = NULL, .p_context = NULL, .p_extend = &g_pdm0_cfg_extend,

#if defined(VECTOR_NUMBER_PDM_SDET)
                .sdet_irq                              = PDM_SDET_IRQn,
#else
		.sdet_irq = FSP_INVALID_VECTOR,
#endif
		.sdet_ipl = (BSP_IRQ_DISABLED),

#if defined(VECTOR_NUMBER_PDM_DAT2)
                .dat_irq                               = PDM_DAT2_IRQn,
#else
		.dat_irq = FSP_INVALID_VECTOR,
#endif
		.dat_ipl = (BSP_IRQ_DISABLED),

#if defined(VECTOR_NUMBER_PDM_ERR2)
                .err_irq                               = PDM_ERR2_IRQn,
#else
		.err_irq = FSP_INVALID_VECTOR,
#endif
		.err_ipl = (12), };

/* Instance structure to use this module. */
const pdm_instance_t g_pdm0 = { .p_ctrl = &g_pdm0_ctrl, .p_cfg = &g_pdm0_cfg,
		.p_api = &g_pdm_on_pdm };
sci_b_uart_instance_ctrl_t g_uart0_ctrl;

sci_b_baud_setting_t g_uart0_baud_setting = {
/* Baud rate calculated with 0.160% error. */.baudrate_bits_b.abcse = 0,
		.baudrate_bits_b.abcs = 0, .baudrate_bits_b.bgdm = 1,
		.baudrate_bits_b.cks = 0, .baudrate_bits_b.brr = 64,
		.baudrate_bits_b.mddr = (uint8_t) 256, .baudrate_bits_b.brme = false };

/** UART extended configuration for UARTonSCI HAL driver */
const sci_b_uart_extended_cfg_t g_uart0_cfg_extend = { .clock =
		SCI_B_UART_CLOCK_INT,
		.rx_edge_start = SCI_B_UART_START_BIT_FALLING_EDGE, .noise_cancel =
				SCI_B_UART_NOISE_CANCELLATION_DISABLE, .rx_fifo_trigger =
				SCI_B_UART_RX_FIFO_TRIGGER_MAX, .p_baud_setting =
				&g_uart0_baud_setting, .flow_control =
				SCI_B_UART_FLOW_CONTROL_RTS,
#if 0xFF != 0xFF
                .flow_control_pin       = BSP_IO_PORT_FF_PIN_0xFF,
                #else
		.flow_control_pin = (bsp_io_port_pin_t) UINT16_MAX,
#endif
		.rs485_setting = { .enable = SCI_B_UART_RS485_DISABLE, .polarity =
				SCI_B_UART_RS485_DE_POLARITY_HIGH, .assertion_time = 1,
				.negation_time = 1, }, .delay_cycles = 0, };

/** UART interface configuration */
const uart_cfg_t g_uart0_cfg = { .channel = 8, .data_bits = UART_DATA_BITS_8,
		.parity = UART_PARITY_OFF, .stop_bits = UART_STOP_BITS_1, .p_callback =
				console_uart_callback, .p_context = NULL, .p_extend =
				&g_uart0_cfg_extend,
#define RA_NOT_DEFINED (1)
#if (RA_NOT_DEFINED == RA_NOT_DEFINED)
		.p_transfer_tx = NULL,
#else
                .p_transfer_tx       = &RA_NOT_DEFINED,
#endif
#if (RA_NOT_DEFINED == RA_NOT_DEFINED)
		.p_transfer_rx = NULL,
#else
                .p_transfer_rx       = &RA_NOT_DEFINED,
#endif
#undef RA_NOT_DEFINED
		.rxi_ipl = (12), .txi_ipl = (12), .tei_ipl = (12), .eri_ipl = (12),
#if defined(VECTOR_NUMBER_SCI8_RXI)
                .rxi_irq             = VECTOR_NUMBER_SCI8_RXI,
#else
		.rxi_irq = FSP_INVALID_VECTOR,
#endif
#if defined(VECTOR_NUMBER_SCI8_TXI)
                .txi_irq             = VECTOR_NUMBER_SCI8_TXI,
#else
		.txi_irq = FSP_INVALID_VECTOR,
#endif
#if defined(VECTOR_NUMBER_SCI8_TEI)
                .tei_irq             = VECTOR_NUMBER_SCI8_TEI,
#else
		.tei_irq = FSP_INVALID_VECTOR,
#endif
#if defined(VECTOR_NUMBER_SCI8_ERI)
                .eri_irq             = VECTOR_NUMBER_SCI8_ERI,
#else
		.eri_irq = FSP_INVALID_VECTOR,
#endif
		};

/* Instance structure to use this module. */
const uart_instance_t g_uart0 = { .p_ctrl = &g_uart0_ctrl,
		.p_cfg = &g_uart0_cfg, .p_api = &g_uart_on_sci_b };
iic_master_instance_ctrl_t g_i2c_master0_ctrl;
const iic_master_extended_cfg_t g_i2c_master0_extend =
		{ .timeout_mode = IIC_MASTER_TIMEOUT_MODE_SHORT, .timeout_scl_low =
				IIC_MASTER_TIMEOUT_SCL_LOW_ENABLED, .smbus_operation = 0,
				/* Actual calculated bitrate: 97809. Actual calculated duty cycle: 49%. */.clock_settings.brl_value =
						17, .clock_settings.brh_value = 16,
				.clock_settings.cks_value = 4, .clock_settings.sddl_value = 0,
				.clock_settings.dlcs_value = 0, };
const i2c_master_cfg_t g_i2c_master0_cfg = { .channel = 1, .rate =
		I2C_MASTER_RATE_STANDARD, .slave = 0x08, .addr_mode =
		I2C_MASTER_ADDR_MODE_7BIT,
#define RA_NOT_DEFINED (1)
#if (RA_NOT_DEFINED == RA_NOT_DEFINED)
		.p_transfer_tx = NULL,
#else
                .p_transfer_tx       = &RA_NOT_DEFINED,
#endif
#if (RA_NOT_DEFINED == RA_NOT_DEFINED)
		.p_transfer_rx = NULL,
#else
                .p_transfer_rx       = &RA_NOT_DEFINED,
#endif
#undef RA_NOT_DEFINED
		.p_callback = i2c_master_callback, .p_context = NULL,
#if defined(VECTOR_NUMBER_IIC1_RXI)
    .rxi_irq             = VECTOR_NUMBER_IIC1_RXI,
#else
		.rxi_irq = FSP_INVALID_VECTOR,
#endif
#if defined(VECTOR_NUMBER_IIC1_TXI)
    .txi_irq             = VECTOR_NUMBER_IIC1_TXI,
#else
		.txi_irq = FSP_INVALID_VECTOR,
#endif
#if defined(VECTOR_NUMBER_IIC1_TEI)
    .tei_irq             = VECTOR_NUMBER_IIC1_TEI,
#else
		.tei_irq = FSP_INVALID_VECTOR,
#endif
#if defined(VECTOR_NUMBER_IIC1_ERI)
    .eri_irq             = VECTOR_NUMBER_IIC1_ERI,
#else
		.eri_irq = FSP_INVALID_VECTOR,
#endif
		.ipl = (12), .p_extend = &g_i2c_master0_extend, };
/* Instance structure to use this module. */
const i2c_master_instance_t g_i2c_master0 = { .p_ctrl = &g_i2c_master0_ctrl,
		.p_cfg = &g_i2c_master0_cfg, .p_api = &g_i2c_master_on_iic };
void g_hal_init(void) {
	g_common_init();
}
