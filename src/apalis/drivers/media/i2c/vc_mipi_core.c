#include "vc_mipi_core.h"
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/delay.h>

//#define TRACE printk("        TRACE [vc-mipi] vc_mipi_core.c --->  %s : %d", __FUNCTION__, __LINE__);
#define TRACE


// ------------------------------------------------------------------------------------------------
//  Helper Functions for I2C Communication

#define U_BYTE(value) (__u8)((value >> 24) & 0xff)
#define H_BYTE(value) (__u8)((value >> 16) & 0xff)
#define M_BYTE(value) (__u8)((value >>  8) & 0xff)
#define L_BYTE(value) (__u8)( value        & 0xff)

int i2c_read_reg(struct i2c_client *client, const __u16 addr)
{
    __u8 buf[2] = {addr >> 8, addr & 0xff};
    int ret;
    struct i2c_msg msgs[] = {
        {
            .addr  = client->addr,
            .flags = 0,
            .len   = 2,
            .buf   = buf,
        }, {
            .addr  = client->addr,
            .flags = I2C_M_RD,
            .len   = 1,
            .buf   = buf,
        },
    };

    ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
    if (ret < 0) {
        dev_err(&client->dev, "[vc-mipi driver common] Reading register %x from %x failed\n",
             addr, client->addr);
        return ret;
    }

    return buf[0];
}

int i2c_write_reg(struct i2c_client *client, const __u16 addr, const __u8 data)
{
    struct i2c_adapter *adap = client->adapter;
    struct i2c_msg msg;
    __u8 tx[3];
    int ret;

    msg.addr = client->addr;
    msg.buf = tx;
    msg.len = 3;
    msg.flags = 0;
    tx[0] = addr >> 8;
    tx[1] = addr & 0xff;
    tx[2] = data;
    ret = i2c_transfer(adap, &msg, 1);
    mdelay(2);

    return ret == 1 ? 0 : -EIO;
}


// ------------------------------------------------------------------------------------------------
//  Helper Functions for the VC MIPI Controller Module

#define MOD_ID_OV9281       0x9281
#define MOD_ID_IMX183       0x0183
#define MOD_ID_IMX226       0x0226
#define MOD_ID_IMX250       0x0250
#define MOD_ID_IMX252       0x0252
#define MOD_ID_IMX273       0x0273
#define MOD_ID_IMX290       0x0290
#define MOD_ID_IMX296       0x0296
#define MOD_ID_IMX297       0x0297
#define MOD_ID_IMX327       0x0327
#define MOD_ID_IMX415       0x0415

#define MOD_REG_RESET       0x0100 // register 0 [0x0100]: reset and init register (R/W)
#define MOD_REG_STATUS      0x0101 // register 1 [0x0101]: status (R) 
#define MOD_REG_MODE        0x0102 // register 2 [0x0102]: initialisation mode (R/W)
#define MOD_REG_IOCTRL      0x0103 // register 3 [0x0103]: input/output control (R/W)
#define MOD_REG_MOD_ADDR    0x0104 // register 4 [0x0104]: module i2c address (R/W, default: 0x10)
#define MOD_REG_SEN_ADDR    0x0105 // register 5 [0x0105]: sensor i2c address (R/W, default: 0x1A)
#define MOD_REG_OUTPUT      0x0106 // register 6 [0x0106]: output signal override register (R/W, default: 0x00)
#define MOD_REG_INPUT       0x0107 // register 7 [0x0107]: input signal status register (R)
#define MOD_REG_EXTTRIG     0x0108 // register 8 [0x0108]: external trigger enable (R/W, default: 0x00)
#define MOD_REG_EXPO_L      0x0109 // register 9  [0x0109]: exposure LSB (R/W, default: 0x10)
#define MOD_REG_EXPO_M      0x010A // register 10 [0x010A]: exposure 	   (R/W, default: 0x27)
#define MOD_REG_EXPO_H      0x010B // register 11 [0x010B]: exposure     (R/W, default: 0x00)
#define MOD_REG_EXPO_U      0x010C // register 12 [0x010C]: exposure MSB (R/W, default: 0x00)
#define MOD_REG_RETRIG_L    0x010D // register 13 [0x010D]: retrigger LSB (R/W, default: 0x40)
#define MOD_REG_RETRIG_M    0x010E // register 14 [0x010E]: retrigger     (R/W, default: 0x2D)
#define MOD_REG_RETRIG_H    0x010F // register 15 [0x010F]: retrigger     (R/W, default: 0x29)
#define MOD_REG_RETRIG_U    0x0110 // register 16 [0x0110]: retrigger MSB (R/W, default: 0x00)

#define REG_RESET_PWR_UP    0x00
#define REG_RESET_SENSOR    0x01   // reg0[0] = 0 sensor reset the sensor is held in reset when this bit is 1
#define REG_RESET_PWR_DOWN  0x02   // reg0[1] = 0 power down power for the sensor is switched off

#define REG_STATUS_NO_COM   0x00   // reg1[7:0] = 0x00 default, no communication with sensor possible
#define REG_STATUS_READY    0x80   // reg1[7:0] = 0x80 sensor ready after successful initialization sequence
#define REG_STATUS_ERROR    0x01   // reg1[7:0] = 0x01 internal error during initialization



void vc_mipi_dump_reg_value(struct device* dev, int addr, int reg) 
{
        int sval = 0;   // short 2-byte value
        if (addr & 1) { // odd addr
            sval |= (int)reg << 8;
            dev_err(dev, "[vc-mipi module] addr=0x%04x reg=0x%04x\n",addr+0x1000-1,sval);
        }     
}

void vc_mipi_dump_hw_desc(struct device* dev, struct vc_mipi_mod_desc* mod_desc)
{
        dev_info(dev, "[vc-mipi module] VC MIPI Module - Hardware Descriptor\n");
        dev_info(dev, "[vc-mipi module] [ MAGIC  ] [ %s ]\n", mod_desc->magic);
        dev_info(dev, "[vc-mipi module] [ MANUF. ] [ %s ] [ MID=0x%04x ]\n", mod_desc->manuf, mod_desc->manuf_id);
        dev_info(dev, "[vc-mipi module] [ SENSOR ] [ %s %s ]\n", mod_desc->sen_manuf, mod_desc->sen_type);
        dev_info(dev, "[vc-mipi module] [ MODULE ] [ ID=0x%04x ] [ REV=0x%04x ]\n", mod_desc->mod_id, mod_desc->mod_rev);
        dev_info(dev, "[vc-mipi module] [ MODES  ] [ NR=0x%04x ] [ BPM=0x%04x ]\n", mod_desc->nr_modes, mod_desc->bytes_per_mode);
}

struct i2c_client* vc_mipi_mod_get_client(struct i2c_adapter *adapter, __u8 addr)
{
        struct i2c_board_info info = {
                I2C_BOARD_INFO("i2c", addr),
                };
        unsigned short addr_list[2] = { addr, I2C_CLIENT_END };
        return i2c_new_probed_device(adapter, &info, addr_list, NULL);
}

int vc_mipi_mod_module_power_down(struct i2c_client* client_mod) 
{
        TRACE
        return i2c_write_reg(client_mod, MOD_REG_RESET, REG_RESET_PWR_DOWN);
}

int vc_mipi_mod_module_power_up(struct i2c_client* client_mod) 
{
        TRACE
        return i2c_write_reg(client_mod, MOD_REG_RESET, REG_RESET_PWR_DOWN);
}

int vc_mipi_mod_get_status(struct i2c_client* client_mod) 
{
        TRACE
        return i2c_read_reg(client_mod, MOD_REG_STATUS);
}

int vc_mipi_mod_wait_until_module_is_ready(struct i2c_client* client_mod) 
{
        struct i2c_client* client = client_mod;
        struct device* dev = &client->dev;
        int status;
        int try;

        TRACE

        status = REG_STATUS_NO_COM;
        try = 0;
        while(status == REG_STATUS_NO_COM && try < 10) {
                mdelay(100);
                status = vc_mipi_mod_get_status(client_mod);
                try++;
        }
        if(status == REG_STATUS_ERROR) {
                dev_err(dev, "[vc-mipi module] %s(): Internal Error!", __func__);
        }
        return status;
}

int vc_mipi_mod_set_mode(struct i2c_client* client_mod, int mode)
{
        TRACE
        return i2c_write_reg(client_mod, MOD_REG_MODE, mode);
} 

int vc_mipi_mod_reset_module(struct i2c_client* client_mod, int mode)
{
        int ret;
        
        TRACE

        ret = vc_mipi_mod_module_power_down(client_mod);
        if (ret) {
                return -EIO;
        }
        // TODO: Check what if module_mode < 0
        mdelay(200);
        // TODO: Check status and print it!?
        // TODO: Check if it is neccessary to set mode when module is down.
        ret = vc_mipi_mod_set_mode(client_mod, mode);
        if (ret) {
                return -EIO;
        }
        ret = vc_mipi_mod_module_power_up(client_mod);
        if (ret) {
                return -EIO;
        }
        ret = vc_mipi_mod_wait_until_module_is_ready(client_mod);
        if (ret) {
                return -EIO;
        }
        return 0;
}

struct i2c_client *vc_mipi_mod_setup(struct i2c_client *client_sen, struct vc_mipi_mod_desc *desc)
{
        struct i2c_adapter* adapter = client_sen->adapter;
        struct device* dev_sen = &client_sen->dev;
        struct i2c_client *client_mod;
        struct device* dev_mod;
        int ret;

        TRACE

        if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
                dev_err(dev_sen, "[vc-mipi module] %s(): I2C-Adapter doesn't support I2C_FUNC_SMBUS_BYTE\n", __FUNCTION__);
                return 0;
        }

        // TODO: Load module address from DT.
        client_mod = vc_mipi_mod_get_client(adapter, 0x10); 
        dev_mod = &client_mod->dev;
        if (client_mod) {
                int addr,reg;
                for (addr=0; addr<sizeof(*desc); addr++) {
                        reg = i2c_read_reg(client_mod, addr+0x1000);
                        if (reg < 0) {
                                i2c_unregister_device(client_mod);
                                return 0;
                        }
                        *((char *)(desc)+addr)=(char)reg;

                        //vc_mipi_dump_reg_value(dev, addr, reg);
                }

                vc_mipi_dump_hw_desc(dev_mod, desc);
        }

        dev_info(dev_mod, "[vc-mipi module] Reset VC MIPI Sensor");
        // TODO: Set module mode in last parameter.
        ret = vc_mipi_mod_reset_module(client_mod, 0); 
        if (ret) {
                return 0;
        }
        dev_info(dev_mod, "[vc-mipi module] VC MIPI Sensor succesfully initialized.");
        return client_mod;
}

int vc_mipi_mod_is_color_module(struct vc_mipi_mod_desc *desc)
{
        TRACE

        if(desc->sen_type) {
                __u32 len = strnlen(desc->sen_type, 16);
                if (len > 0 && len < 17) {
                        return *(desc->sen_type+len-1) == 'C';
                }
        }
        return 0;
}

int vc_mipi_mod_set_exposure(struct i2c_client* client, __u32 value, __u32 sen_clk)
{
        struct device* dev = &client->dev;
        int ret;

        __u32 exposure = (value * (sen_clk/1000000)); 

        dev_info(dev, "[vc-mipi module] Write exposure = 0x%08x (%u)\n", exposure, exposure);

        ret  = i2c_write_reg(client, MOD_REG_EXPO_U, U_BYTE(exposure));
        ret |= i2c_write_reg(client, MOD_REG_EXPO_H, H_BYTE(exposure));
        ret |= i2c_write_reg(client, MOD_REG_EXPO_M, M_BYTE(exposure));
        ret |= i2c_write_reg(client, MOD_REG_EXPO_L, L_BYTE(exposure));
        return ret;
}



// ------------------------------------------------------------------------------------------------
//  Helper Functions for the VC MIPI Sensors


#define IMX226_SEN_REG_GAIN_L	0x0009
#define IMX226_SEN_REG_GAIN_M	0x000A
#define IMX226_SEN_REG_EXPO_L	0x000B
#define IMX226_SEN_REG_EXPO_M	0x000C

#define IMX226_SEN_REG_HMAX_L   0x7002
#define IMX226_SEN_REG_HMAX_H   0x7003
#define IMX226_SEN_REG_VMAX_L   0x7004
#define IMX226_SEN_REG_VMAX_M   0x7005
#define IMX226_SEN_REG_VMAX_H   0x7006

#define IMX226_EXPO_TIME_MIN1  	160
#define IMX226_EXPO_TIME_MIN2 	74480
#define IMX226_EXPO_TIME_MAX  	10000000
#define IMX226_EXPO_NRLINES    	3694
#define IMX226_EXPO_TOFFSET	47563  		// tOffset (U32)(2.903 * 16384.0)
#define IMX226_EXPO_H1PERIOD	327680 		// h1Period 20.00us => (U32)(20.000 * 16384.0)
#define IMX226_EXPO_VMAX	3728


__u32 vc_mipi_sen_read_vmax(struct i2c_client* client) 
{
        struct device* dev = &client->dev;
        int reg;
        __u32 vmax = 0;

        reg = i2c_read_reg(client, IMX226_SEN_REG_VMAX_H);
        if(reg) vmax = reg;
        reg = i2c_read_reg(client, IMX226_SEN_REG_VMAX_M);
        if(reg) vmax = (vmax << 8) | reg;
        reg = i2c_read_reg(client, IMX226_SEN_REG_VMAX_L);
        if(reg) vmax = (vmax << 8) | reg;

        dev_info(dev, "[vc-mipi sensor] Read vmax = 0x%08x (%d)\n", vmax, vmax);

        return vmax;
}

int vc_mipi_sen_write_vmax(struct i2c_client* client, __u32 vmax)
{
        struct device* dev = &client->dev;
        int ret;

        dev_info(dev, "[vc-mipi sensor] Write vmax = 0x%08x (%d)\n", vmax, vmax);

        ret  = i2c_write_reg(client, IMX226_SEN_REG_VMAX_H, H_BYTE(vmax));
        ret |= i2c_write_reg(client, IMX226_SEN_REG_VMAX_M, M_BYTE(vmax));
        ret |= i2c_write_reg(client, IMX226_SEN_REG_VMAX_L, L_BYTE(vmax));
        return ret;
}

int vc_mipi_sen_write_exposure(struct i2c_client* client, __u32 exposure)
{
        struct device* dev = &client->dev;
        int ret;

        dev_info(dev, "[vc-mipi sensor] Write exposure = 0x%08x (%d)\n", exposure, exposure);

        ret  = i2c_write_reg(client, IMX226_SEN_REG_EXPO_M, M_BYTE(exposure));
        ret |= i2c_write_reg(client, IMX226_SEN_REG_EXPO_L, L_BYTE(exposure));
        return ret;
}

int vc_mipi_sen_set_exposure(struct i2c_client *client, int value)
{
        struct device* dev = &client->dev;
        __u32 exposure = 0;
        int ret=0;

        // TRACE

        // TODO: It is assumed, that the exposure value is valid => remove clamping.
        if (value < IMX226_EXPO_TIME_MIN1)
                value = IMX226_EXPO_TIME_MIN1;
        if (value > IMX226_EXPO_TIME_MAX)
                value = IMX226_EXPO_TIME_MAX;      
        
        if (value < IMX226_EXPO_TIME_MIN2) {
                // TODO: Find out which version is correct.
                // __u32 vmax = vc_mipi_sen_read_vmax(client);
                __u32 vmax = IMX226_EXPO_VMAX;

                // exposure = (NumberOfLines - exp_time / 1Hperiod + toffset / 1Hperiod )
                // shutter = {VMAX - SHR}*HMAX + 209(157) clocks
                exposure = (vmax -  ((__u32)(value) * 16384 - IMX226_EXPO_TOFFSET)/IMX226_EXPO_H1PERIOD);
                dev_info(dev, "[vc-mipi sensor] SHS = %d \n", exposure);

                // TODO: Is it realy nessecary to write the same vmax value back? 
                ret  = vc_mipi_sen_write_vmax(client, vmax);
                ret |= vc_mipi_sen_write_exposure(client, exposure);
        
        } else {
                // exposure = 5 + ((unsigned long long)(value * 16384) - tOffset)/h1Period;
                __u64 divresult = ((__u64)value * 16384) - IMX226_EXPO_TOFFSET;
                // __u32 divisor   = IMX226_EXPO_H1PERIOD;
                // __u32 remainder = (__u32)(do_div(divresult, divisor)); // caution: division result value at dividend!
                exposure = 5 + (__u32)divresult;
                dev_info(dev, "[vc-mipi sensor] VMAX = %d \n", exposure);
                
                ret  = vc_mipi_sen_write_exposure(client, 0x0004);
                ret |= vc_mipi_sen_write_vmax(client, exposure);
        }

        return ret;
}

int vc_mipi_sen_set_gain(struct i2c_client *client, int value)
{
	struct device *dev = &client->dev;
	int ret = 0;

	// TRACE

	ret = i2c_write_reg(client, IMX226_SEN_REG_GAIN_M, M_BYTE(value));
        ret |= i2c_write_reg(client, IMX226_SEN_REG_GAIN_L, L_BYTE(value));
        if (ret) 
                dev_err(dev, "[vc-mipi sensor] %s: Couldn't set 'Gain' (error=%d)\n", __FUNCTION__, ret);

	return ret;
}