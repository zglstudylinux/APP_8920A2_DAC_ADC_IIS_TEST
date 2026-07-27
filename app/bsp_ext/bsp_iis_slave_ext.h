#ifndef _BSP_IIS_SLAVE_EXT_H
#define _BSP_IIS_SLAVE_EXT_H

//A4 任务: 在应用层重写 iis_slave_ram_rx_2_dac()
//通过 GCC --wrap=iis_slave_ram_rx_2_dac 链接选项
//main.c 调用 iis_slave_ram_rx_2_dac() 时链接器自动改写到 __wrap_iis_slave_ram_rx_2_dac()

void __wrap_iis_slave_ram_rx_2_dac(void);

#endif