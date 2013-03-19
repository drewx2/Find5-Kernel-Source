/* drivers/input/touchscreen/ektf2k.c - ELAN EKTF2K verions of driver
 *
 * Copyright (C) 2011 Elan Microelectronics Corporation.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * 2011/12/06: The first release, version 0x0001
 * 2012/2/15:  The second release, version 0x0002 for new bootcode
 * 2012/5/8:   Release version 0x0003 for china market
 *             Integrated 2 and 5 fingers driver code together and
 *             auto-mapping resolution.
 */

//#define ELAN_BUFFER_MODE

#include <linux/module.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/earlysuspend.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/device.h>
#include <linux/jiffies.h>
#include <linux/miscdevice.h>

// for linux 2.6.36.3
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <asm/ioctl.h>
#include <linux/i2c/ektf2k.h> 

#define PACKET_SIZE		22 	

#define SOFTKEY_AXIS_VER 0


#define PWR_STATE_DEEP_SLEEP	0
#define PWR_STATE_NORMAL		1
#define PWR_STATE_MASK			BIT(3)

#define CMD_S_PKT			0x52
#define CMD_R_PKT			0x53
#define CMD_W_PKT			0x54

#define HELLO_PKT			0x55
#define NORMAL_PKT			0x5D

#define TWO_FINGERS_PKT		0x5A
#define FIVE_FINGERS_PKT	0x6D
#define TEN_FINGERS_PKT		0x62

#define RESET_PKT			0x77
#define CALIB_PKT			0xA8

// modify
/* OPPO 2012-09-20 liujun Delete begin for no reset gpio */
#if 0
#define SYSTEM_RESET_PIN_SR 	10
#endif
/* OPPO 2012-09-20 liujun Delete end */

//Add these Define
#define IAP_IN_DRIVER_MODE 	1
#define IAP_PORTION			0
#define PAGERETRY			30
#define IAPRESTART			5


// For Firmware Update 
#define ELAN_IOCTLID	0xD0
#define IOCTL_I2C_SLAVE				_IOW(ELAN_IOCTLID, 1, int)
#define IOCTL_MAJOR_FW_VER			_IOR(ELAN_IOCTLID, 2, int)
#define IOCTL_MINOR_FW_VER			_IOR(ELAN_IOCTLID, 3, int)
#define IOCTL_RESET					_IOR(ELAN_IOCTLID, 4, int)
#define IOCTL_IAP_MODE_LOCK			_IOR(ELAN_IOCTLID, 5, int)
#define IOCTL_CHECK_RECOVERY_MODE	_IOR(ELAN_IOCTLID, 6, int)
#define IOCTL_FW_VER				_IOR(ELAN_IOCTLID, 7, int)
#define IOCTL_X_RESOLUTION			_IOR(ELAN_IOCTLID, 8, int)
#define IOCTL_Y_RESOLUTION			_IOR(ELAN_IOCTLID, 9, int)
#define IOCTL_FW_ID					_IOR(ELAN_IOCTLID, 10, int)
#define IOCTL_ROUGH_CALIBRATE		_IOR(ELAN_IOCTLID, 11, int)
#define IOCTL_IAP_MODE_UNLOCK		_IOR(ELAN_IOCTLID, 12, int)
#define IOCTL_I2C_INT				_IOR(ELAN_IOCTLID, 13, int)
#define IOCTL_RESUME				_IOR(ELAN_IOCTLID, 14, int)
#define IOCTL_POWER_LOCK			_IOR(ELAN_IOCTLID, 15, int)
#define IOCTL_POWER_UNLOCK			_IOR(ELAN_IOCTLID, 16, int)

#define CUSTOMER_IOCTLID	0xA0
#define IOCTL_CIRCUIT_CHECK			_IOR(CUSTOMER_IOCTLID, 1, int)

#if IAP_PORTION
#define IOCTL_FW_UPDATE				_IOR(ELAN_IOCTLID, 17, int)
#define IOCTL_GET_UPDATE_PROGREE	_IOR(CUSTOMER_IOCTLID,  2, int)
#endif


uint8_t RECOVERY=0x00;
//int FW_VERSION=0x00;
//int X_RESOLUTION=0x00;  
//int Y_RESOLUTION=0x00;
//int FW_ID=0x00;
int work_lock=0x00;
int power_lock=0x00;
int circuit_ver=0x01;
int y_limit = 0;
/*++++i2c transfer start+++++++*/
int file_fops_addr=0x15;
/*++++i2c transfer end+++++++*/



#if SOFTKEY_AXIS_VER

static int key_pressed = -1;

struct osd_offset{
	int left_x;
	int right_x;
	unsigned int key_event;
};

static struct osd_offset OSD_mapping[] = { // Range need define by Case!
	{20,  200, KEY_MENU},	//menu_left_x, menu_right_x, KEY_MENU
	{220, 400, KEY_HOME},	//home_left_x, home_right_x, KEY_HOME
	{420, 620, KEY_BACK},	//back_left_x, back_right_x, KEY_BACK
	//{600, 779, KEY_SEARCH},	//search_left_x, search_right_x, KEY_SEARCH
};
#endif 


#if IAP_PORTION
uint8_t ic_status=0x00;	//0:OK 1:master fail 2:slave fail
int update_progree=0;
uint8_t I2C_DATA[3] = {0x15, 0x20, 0x21};/*I2C devices address*/  
int is_OldBootCode = 0; // 0:new 1:old


/*The newest firmware, if update must be changed here*/
static uint8_t file_fw_data[] = {
/* OPPO 2012-09-08 liujun Delete begin for no firmware file */
#if 0
#include "elan_ts_fw_data_i6_success_0001.i"
#endif
/* OPPO 2012-09-08 liujun Delete end */
};


enum
{
	PageSize		= 132,
	PageNum			= 249,
	ACK_Fail		= 0x00,
	ACK_OK			= 0xAA,
	ACK_REWRITE		= 0x55,
};

enum
{
	E_FD			= -1,
};
#endif

struct elan_ktf2k_ts_data {
	struct i2c_client *client;
	struct input_dev *input_dev;
	struct workqueue_struct *elan_wq;
	struct work_struct work;
	struct early_suspend early_suspend;
	int intr_gpio;
	int (*power)(int on);
// Firmware Information
	int fw_ver;
	int fw_id;
	int x_resolution;
	int y_resolution;
// For Firmare Update 
	struct miscdevice firmware;
};

static struct elan_ktf2k_ts_data *private_ts;
static int __fw_packet_handler(struct i2c_client *client);
static int elan_ktf2k_ts_rough_calibrate(struct i2c_client *client);
static int elan_ktf2k_ts_resume(struct i2c_client *client);

#if IAP_PORTION
int Update_FW_One(/*struct file *filp,*/ struct i2c_client *client, int recovery);
static int __hello_packet_handler(struct i2c_client *client);
#endif
// For Firmware Update 
int elan_iap_open(struct inode *inode, struct file *filp)
{ 
	printk("[ELAN]into elan_iap_open\n");
	if (private_ts == NULL)
		printk("private_ts is NULL~~~");

	return 0;
}

int elan_iap_release(struct inode *inode, struct file *filp)
{    
	return 0;
}

static ssize_t elan_iap_write(struct file *filp, const char *buff, size_t count, loff_t *offp)
{  
	int ret;
	char *tmp;

	/*++++i2c transfer start+++++++*/    	
	struct i2c_adapter *adap = private_ts->client->adapter;  	
	struct i2c_msg msg;
	/*++++i2c transfer end+++++++*/	

	printk("[ELAN]into elan_iap_write\n");

	if (count > 8192)
		count = 8192;

	tmp = kmalloc(count, GFP_KERNEL);

	if (tmp == NULL)
		return -ENOMEM;

	if (copy_from_user(tmp, buff, count))
	{
		return -EFAULT;
	}

/*++++i2c transfer start+++++++*/
#if 1
	//down(&worklock);
	msg.addr = file_fops_addr;
	msg.flags = 0x00;// 0x00
	msg.len = count;
	msg.buf = (char *)tmp;
	//up(&worklock);
	ret = i2c_transfer(adap, &msg, 1);
#else

	ret = i2c_master_send(private_ts->client, tmp, count);
#endif
/*++++i2c transfer end+++++++*/

	//if (ret != count) printk("ELAN i2c_master_send fail, ret=%d \n", ret);
	kfree(tmp);
	//return ret;
	return (ret == 1) ? count : ret;

}

ssize_t elan_iap_read(struct file *filp, char *buff, size_t count, loff_t *offp)
{    
	char *tmp;
	int ret;  
	long rc;
/*++++i2c transfer start+++++++*/
	struct i2c_adapter *adap = private_ts->client->adapter;
	struct i2c_msg msg;
/*++++i2c transfer end+++++++*/
	printk("[ELAN]into elan_iap_read\n");
	if (count > 8192)
		count = 8192;

	tmp = kmalloc(count, GFP_KERNEL);

	if (tmp == NULL)
		return -ENOMEM;
/*++++i2c transfer start+++++++*/
#if 1
	//down(&worklock);
	msg.addr = file_fops_addr;
	//msg.flags |= I2C_M_RD;
	msg.flags = 0x00;
	msg.flags |= I2C_M_RD;
	msg.len = count;
	msg.buf = tmp;
	//up(&worklock);
	ret = i2c_transfer(adap, &msg, 1);
#else
	ret = i2c_master_recv(private_ts->client, tmp, count);
#endif
/*++++i2c transfer end+++++++*/
	if (ret >= 0)
		rc = copy_to_user(buff, tmp, count);

	kfree(tmp);

	//return ret;
	return (ret == 1) ? count : ret;

}

static long elan_iap_ioctl(/*struct inode *inode,*/ struct file *filp,    unsigned int cmd, unsigned long arg)
{
	int __user *ip = (int __user *)arg;
	printk("[ELAN]into elan_iap_ioctl\n");
	printk("cmd value %x\n",cmd);

	switch (cmd) {        
		case IOCTL_I2C_SLAVE: 
			//private_ts->client->addr = (int __user)arg;
			file_fops_addr = 0x15;
			break;   
		case IOCTL_MAJOR_FW_VER:            
			break;        
		case IOCTL_MINOR_FW_VER:            
			break;        
		case IOCTL_RESET:
// modify
/* OPPO 2012-09-20 liujun Delete begin for no reset gpio */
#if 0
			gpio_set_value(SYSTEM_RESET_PIN_SR, 0);
			msleep(20);
			gpio_set_value(SYSTEM_RESET_PIN_SR, 1);
			msleep(100);
#endif
/* OPPO 2012-09-20 liujun Delete end */

			break;
		case IOCTL_IAP_MODE_LOCK:
			if(work_lock==0)
			{
				work_lock=1;
				disable_irq(private_ts->client->irq);
				cancel_work_sync(&private_ts->work);
			}
			break;
		case IOCTL_IAP_MODE_UNLOCK:
			if(work_lock==1)
			{
				work_lock=0;
				enable_irq(private_ts->client->irq);
			}
			break;
		case IOCTL_CHECK_RECOVERY_MODE:
			return RECOVERY;
			break;
		case IOCTL_FW_VER:
			__fw_packet_handler(private_ts->client);
			//return FW_VERSION;
			return private_ts->fw_ver;
			break;
		case IOCTL_X_RESOLUTION:
			__fw_packet_handler(private_ts->client);
			//return X_RESOLUTION;
			return private_ts->x_resolution;
			break;
		case IOCTL_Y_RESOLUTION:
			__fw_packet_handler(private_ts->client);
			//return Y_RESOLUTION;
			return private_ts->y_resolution;
			break;
		case IOCTL_FW_ID:
			__fw_packet_handler(private_ts->client);
			//return FW_ID;
			return private_ts->fw_id;
			break;
		case IOCTL_ROUGH_CALIBRATE:
			return elan_ktf2k_ts_rough_calibrate(private_ts->client);
		case IOCTL_I2C_INT:
			put_user(gpio_get_value(private_ts->intr_gpio), ip);
			break;
		case IOCTL_RESUME:
			elan_ktf2k_ts_resume(private_ts->client);
			break;
		case IOCTL_CIRCUIT_CHECK:
			return circuit_ver;
			break;
		case IOCTL_POWER_LOCK:
			power_lock=1;
			break;
		case IOCTL_POWER_UNLOCK:
			power_lock=0;
			break;
#if IAP_PORTION
		case IOCTL_GET_UPDATE_PROGREE:
			update_progree=(int __user)arg;
			break; 

		case IOCTL_FW_UPDATE:
			Update_FW_One(private_ts->client, 0);
#endif
		default:
			break;
	}
	return 0;
}

struct file_operations elan_touch_fops = {    
	.open =    elan_iap_open,    
	.write =   elan_iap_write,    
	.read =    elan_iap_read,    
	.release = elan_iap_release,    
	.unlocked_ioctl = elan_iap_ioctl, 
 };

#include <linux/regulator/consumer.h>
static struct regulator *vreg_tp_2P8V = NULL;
static struct regulator *vreg_tp_1P8V = NULL;
static DEFINE_MUTEX(TP_POWER_LOCK);

static int init_tp_regulator(void)
{
	int rc;
	if(vreg_tp_2P8V == NULL)
	{
		vreg_tp_2P8V = regulator_get(NULL, "8921_l16");						
		if (IS_ERR(vreg_tp_2P8V))
			return PTR_ERR(vreg_tp_2P8V);

		rc = regulator_set_voltage(vreg_tp_2P8V, 2800000, 2800000);
		if (rc)
		{
			pr_err("%s: unable to set the voltage for regulator vreg_2P8V\n", __func__);
			regulator_put(vreg_tp_2P8V);
            vreg_tp_2P8V = NULL;
			return rc;
		}
	}
	if(vreg_tp_1P8V == NULL)
	{		
		vreg_tp_1P8V = regulator_get(NULL, "8921_l22");
		if (IS_ERR(vreg_tp_1P8V))
			return PTR_ERR(vreg_tp_1P8V);

		rc = regulator_set_voltage(vreg_tp_1P8V, 1800000, 1800000);
		if (rc)
		{
			pr_err("%s: unable to set the voltage for regulator vreg_1P8V\n", __func__);
			regulator_put(vreg_tp_1P8V);
            vreg_tp_1P8V = NULL;
			return rc;
		}
	}
	return rc;
}

static int inline enable_tp_regulator(struct regulator **vreg_tp)
{
	int rc = regulator_enable(*vreg_tp);
	if (rc)
	{
		pr_err("%s: unable to enable regulator %s\n",
			__func__, (*vreg_tp == vreg_tp_2P8V)?"vreg_2P8V":"vreg_1P8V");
		regulator_put(*vreg_tp);
		*vreg_tp = NULL;
	}
	return rc;
}

static int inline disable_tp_regulator(struct regulator *vreg_tp)
{
	int rc = regulator_disable(vreg_tp);
	if (rc)
		pr_err("%s: Unable to disable the regulator: %s\n",
			__func__, (vreg_tp == vreg_tp_2P8V)?"vreg_2P8V":"vreg_1P8V");
	return rc;
}

static int ektf2k_touchscreen_power(int on)
{
	int rc = 0;

	mutex_lock(&TP_POWER_LOCK);

	rc = init_tp_regulator();
	if (rc)
		goto oppo_tp_power_return;

	if(on == 0xEF)
	{
		// Power reset
		rc = disable_tp_regulator(vreg_tp_2P8V);
		if (rc)
			goto oppo_tp_power_return;
		msleep(60);

		rc = enable_tp_regulator(&vreg_tp_2P8V);
		if (rc)
			goto oppo_tp_power_return;
		mdelay(200);

		pr_debug("[TSP] %s: power reset\n", __func__);
	} 
	else if (on)
	{
		// Power on
		rc = enable_tp_regulator(&vreg_tp_1P8V);
		if (rc)
			goto oppo_tp_power_return;
		mdelay(10);

		rc = enable_tp_regulator(&vreg_tp_2P8V);
		if (rc)
			goto oppo_tp_power_return;
		mdelay(200);

		pr_debug("[TSP] %s: power on\n", __func__);
	}
	else
	{
		// Power off
		rc = disable_tp_regulator(vreg_tp_2P8V);
		if (rc)
			goto oppo_tp_power_return;
		mdelay(10);

		rc = disable_tp_regulator(vreg_tp_1P8V);
		if (rc)
			goto oppo_tp_power_return;
		mdelay(30);

		pr_debug("[TSP] %s: power off\n", __func__);
	}

oppo_tp_power_return:
	mutex_unlock(&TP_POWER_LOCK);
	return rc;
 }
#if IAP_PORTION
//#define ELAN_KTF2K_TP_RESET_GPIO 26
static void elan_ktf2k_ts_reset(void)
{
#if 0
	int rc = 0; 

	printk("%s\n", __FUNCTION__);

	rc = gpio_tlmm_config(GPIO_CFG(ELAN_KTF2K_TP_RESET_GPIO, 0,
			GPIO_CFG_OUTPUT, GPIO_CFG_PULL_UP,
			GPIO_CFG_2MA), GPIO_CFG_ENABLE);
	if (rc < 0)
	{
		pr_err("%s: linxc say unable to enable sitronix Multitouch Reset!\n", __func__);
		gpio_free(ELAN_KTF2K_TP_RESET_GPIO);	
		return;
	}

	msleep(10);
	gpio_set_value(ELAN_KTF2K_TP_RESET_GPIO, 1);
	mdelay(10);
	gpio_set_value(ELAN_KTF2K_TP_RESET_GPIO, 0);
	mdelay(20);
	gpio_set_value(ELAN_KTF2K_TP_RESET_GPIO, 1);
	msleep(2);
	//gpio_free(SITRONIX_RESET_GPIO);

	msleep(150);
#else
/* OPPO 2012-09-20 liujun Add begin for reset by power */
	if (private_ts->power)
	{
		private_ts->power(0xEF);
	}
/* OPPO 2012-09-20 liujun Add end */
#endif
}


int IAPReset(struct i2c_client *client)
{
	int res;
	int num = 10;
	char recovery_buffer[4];
	//reset
	elan_ktf2k_ts_reset();

	printk("[ELAN] read Hello packet data!\n"); 	  
	res= __hello_packet_handler(client);

	msleep(40);

	if(res == 0x80)
	{

		while(num--)
		{
			res = i2c_master_recv(private_ts->client, recovery_buffer, 4);   //55 aa 33 cc 
			printk("\r[ELAN] recovery byte data:%x,%x,%x,%x \n",recovery_buffer[0],recovery_buffer[1],recovery_buffer[2],recovery_buffer[3]);

			if(recovery_buffer[0]==0x55 && recovery_buffer[1]==0xaa && recovery_buffer[2]==0x33 && recovery_buffer[3]==0xcc)
				break;
		}
		if(num <= 0)
		{
			printk("ELAN iap read response error!\n");
		}
	}
	return res;
}

int EnterISPMode(struct i2c_client *client, uint8_t  *isp_cmd)
{
	char buff[4] = {0};
	int len = 0;

	len = i2c_master_send(private_ts->client, isp_cmd,  sizeof(isp_cmd));
	if (len != sizeof(buff))
	{
		printk("[ELAN] ERROR: EnterISPMode fail! len=%d\r\n", len);
		return -1;
	}
	else
		printk("[ELAN] IAPMode write data successfully! cmd = [%2x, %2x, %2x, %2x]\n", isp_cmd[0], isp_cmd[1], isp_cmd[2], isp_cmd[3]);
	return 0;
}


int ExtractPage(struct file *filp, uint8_t * szPage, int byte)
{
	int len = 0;

	len = filp->f_op->read(filp, szPage,byte, &filp->f_pos);
	if (len != byte) 
	{
		printk("[ELAN] ExtractPage ERROR: read page error, read error. len=%d\r\n", len);
		return -1;
	}

	return 0;
}


int WritePage(uint8_t * szPage, int byte)
{
	int len = 0;

	len = i2c_master_send(private_ts->client, szPage,  byte);
	if (len != byte) 
	{
		printk("[ELAN] ERROR: write page error, write error. len=%d\r\n", len);
		return -1;
	}

	return 0;
}


int GetAckData(struct i2c_client *client)
{
	int len = 0;

	char buff[2] = {0};
	
	len=i2c_master_recv(private_ts->client, buff, sizeof(buff));
	if (len != sizeof(buff))
	{
		printk("[ELAN] ERROR: read data error, write 50 times error. len=%d\r\n", len);
		return -1;
	}

	printk("[ELAN] GetAckData:%x,%x",buff[0],buff[1]);

	if (buff[0] == 0xaa/* && buff[1] == 0xaa*/) 
		return ACK_OK;
	else if (buff[0] == 0x55 && buff[1] == 0x55)
		return ACK_REWRITE;
	else
		return ACK_Fail;

	return 0;
}


void print_progress(int page, int ic_num, int j)
{
	int i, percent,page_tatol,percent_tatol;
	char str[256];
	str[0] = '\0';
	for (i=0; i<((page)/10); i++)
	{
		str[i] = '#';
		str[i+1] = '\0';
	}
	
	page_tatol = page + 249*(ic_num - j);
	percent = ((100*page)/(249));
	percent_tatol = ((100*page_tatol)/(249*ic_num));

	if ((page) == (249))
		percent = 100;

	if ((page_tatol) == (249*ic_num))
		percent_tatol = 100;		

	printk("\rprogress %s| %d%%", str, percent);

	if (page == (249))
		printk("\n");
}


int Update_FW_One(struct i2c_client *client, int recovery)
{
	int res = 0,ic_num = 1;
	//rewriteCnt for PAGE_REWRITE
	int iPage = 0, rewriteCnt = 0; 
	int i = 0;
	uint8_t data;
	
	//struct timeval tv1, tv2,For IAP_RESTART
	int restartCnt = 0;
	
	uint8_t recovery_buffer[4] = {0};
	int byte_count;
	uint8_t *szBuff = NULL;
	int curIndex = 0;
	uint8_t isp_cmd[] = {0x54, 0x00, 0x12, 0x34}; //{0x45, 0x49, 0x41, 0x50};

	printk("\r[ELAN] %s:  ic_num=%d\n", __func__, ic_num);
	
IAP_RESTART:
	
	data=I2C_DATA[0];
	printk("\r[ELAN] %s: address data=0x%x \n", __func__, data);

	if(recovery != 0x80)
	{
        printk("\r[ELAN] Firmware upgrade normal mode !\n");

		elan_ktf2k_ts_reset();

		res = EnterISPMode(private_ts->client, isp_cmd);	 //enter ISP mode
	}
	else
	{
    	printk("\r[ELAN] Firmware upgrade recovery mode !\n");
		res = i2c_master_recv(private_ts->client, recovery_buffer, 4);   //55 aa 33 cc 
		printk("\r[ELAN] recovery byte data:%x,%x,%x,%x \n",recovery_buffer[0],recovery_buffer[1],recovery_buffer[2],recovery_buffer[3]);	
	}

	// Send Dummy Byte	
	printk("\r[ELAN] send one byte data:%x,%x",private_ts->client->addr,data);
	res = i2c_master_send(private_ts->client, &data,  sizeof(data));

	if(res!=sizeof(data))
	{
		printk("[ELAN] dummy error code = %d\n",res);
	}	
	mdelay(10);


	// Start IAP
	for( iPage = 1; iPage <= PageNum; iPage++ ) 
	{
PAGE_REWRITE:
#if 1 
		//szBuff = fw_data + ((iPage-1) * PageSize); 
		for(byte_count = 1; byte_count <= 17; byte_count++)
		{
			if(byte_count != 17)
			{		
				//printk("[ELAN] byte %d\n",byte_count);	
				//printk("curIndex =%d\n",curIndex);
				szBuff = file_fw_data + curIndex;
				curIndex =  curIndex + 8;

				//ioctl(fd, IOCTL_IAP_MODE_LOCK, data);
				res = WritePage(szBuff, 8);
			}
			else
			{
				//printk("byte %d\n",byte_count);
				//printk("curIndex =%d\n",curIndex);
	
				szBuff = file_fw_data + curIndex;
				curIndex =  curIndex + 4;
				
				//ioctl(fd, IOCTL_IAP_MODE_LOCK, data);
				res = WritePage(szBuff, 4); 
			}
		} // end of for(byte_count=1;byte_count<=17;byte_count++)
#endif 


#if 0 // 132byte mode		
		szBuff = file_fw_data + curIndex;
		curIndex =  curIndex + PageSize;
		res = WritePage(szBuff, PageSize);
#endif


		if(iPage == 249 || iPage == 1)
		{
			mdelay(600);
		}
		else
		{
			mdelay(50);
		}
		res = GetAckData(private_ts->client);
		

		if (ACK_OK != res) 
		{
			mdelay(50); 
			printk("[ELAN] ERROR: GetAckData fail! res=%d\r\n", res);
			if ( res == ACK_REWRITE ) 
			{
				rewriteCnt = rewriteCnt + 1;
				if (rewriteCnt == PAGERETRY)
				{
					printk("[ELAN] ID 0x%02x %dth page ReWrite %d times fails!\n", data, iPage, PAGERETRY);
					return E_FD;
				}
				else
				{
					printk("[ELAN] ---%d--- page ReWrite %d times!\n",  iPage, rewriteCnt);
					goto PAGE_REWRITE;
				}
			}
			else
			{
				restartCnt = restartCnt + 1;
				if (restartCnt >= 5)
				{
					printk("[ELAN] ID 0x%02x ReStart %d times fails!\n", data, IAPRESTART);
					return E_FD;
				}
				else
				{
					printk("[ELAN] ===%d=== page ReStart %d times!\n",  iPage, restartCnt);
					goto IAP_RESTART;
				}
			}
		}
		else
		{       printk("  data : 0x%02x ",  data);  
			rewriteCnt=0;
			print_progress(iPage,ic_num,i);
		}
		

		mdelay(10);
	} // end of for(iPage = 1; iPage <= PageNum; iPage++)
	

	if (IAPReset(client) == 0)
	{
		printk("[ELAN] Update ALL Firmware successfully!\n");
		return 0;
	}
	return 0;
}

#endif
// End Firmware Update


#ifdef CONFIG_HAS_EARLYSUSPEND
static void elan_ktf2k_ts_early_suspend(struct early_suspend *h);
static void elan_ktf2k_ts_late_resume(struct early_suspend *h);
#endif

static ssize_t elan_ktf2k_gpio_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret = 0;
	struct elan_ktf2k_ts_data *ts = private_ts;

	//ret = gpio_get_value(ts->intr_gpio);
	//printk(KERN_DEBUG "GPIO_TP_INT_N=%d\n", ts->intr_gpio);
	sprintf(buf, "GPIO_TP_INT_N=%d\n", ts->intr_gpio);
	ret = strlen(buf) + 1;
	return ret;
}

static DEVICE_ATTR(gpio, S_IRUGO, elan_ktf2k_gpio_show, NULL);

static ssize_t elan_ktf2k_vendor_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;
	struct elan_ktf2k_ts_data *ts = private_ts;

	sprintf(buf, "%s_x%4.4x\n", "ELAN_KTF2K", ts->fw_ver);
	ret = strlen(buf) + 1;
	return ret;
}

static DEVICE_ATTR(vendor, S_IRUGO, elan_ktf2k_vendor_show, NULL);

static struct kobject *android_touch_kobj;

static int elan_ktf2k_touch_sysfs_init(struct input_dev *dev)
{
	int ret ;

	android_touch_kobj = &dev->dev.kobj;

	ret = sysfs_create_file(android_touch_kobj, &dev_attr_gpio.attr);
	if (ret)
	{
		printk(KERN_ERR "[elan]%s: sysfs_create_file failed\n", __func__);
		return ret;
	}
	ret = sysfs_create_file(android_touch_kobj, &dev_attr_vendor.attr);
	if (ret)
	{
		printk(KERN_ERR "[elan]%s: sysfs_create_group failed\n", __func__);
		return ret;
	}
	return 0 ;
}

static void elan_touch_sysfs_deinit(void)
{
	sysfs_remove_file(android_touch_kobj, &dev_attr_vendor.attr);
	sysfs_remove_file(android_touch_kobj, &dev_attr_gpio.attr);
	android_touch_kobj = NULL;
}	

static int __elan_ktf2k_ts_poll(struct i2c_client *client)
{
	struct elan_ktf2k_ts_data *ts = i2c_get_clientdata(client);
	int status = 0, retry = 20;

	do
	{
		status = gpio_get_value(ts->intr_gpio);
		//printk("%s: status = %d\n", __func__, status);
		retry--;
		mdelay(20);
	} while (status == 1 && retry > 0);

	printk( "[elan]%s: poll interrupt status %s\n",
			__func__, status == 1 ? "high" : "low");
	return (status == 0 ? 0 : -ETIMEDOUT);
}

static int elan_ktf2k_ts_poll(struct i2c_client *client)
{
	return __elan_ktf2k_ts_poll(client);
}

static int elan_ktf2k_ts_get_data(struct i2c_client *client, uint8_t *cmd,
			uint8_t *buf, size_t size)
{
	int rc;

	dev_dbg(&client->dev, "[elan]%s: enter\n", __func__);

	if (buf == NULL)
		return -EINVAL;

	if ((i2c_master_send(client, cmd, 4)) != 4)
	{
		dev_err(&client->dev,
			"[elan]%s: i2c_master_send failed\n", __func__);
		return -EINVAL;
	}

	rc = elan_ktf2k_ts_poll(client);
	if (rc < 0)
		return -EINVAL;
	else
	{
		if (i2c_master_recv(client, buf, size) != size ||
		    buf[0] != CMD_S_PKT)
			return -EINVAL;
	}

	return 0;
}

static int __hello_packet_handler(struct i2c_client *client)
{
	int rc;
	int retry = 5;
	uint8_t buf_recv[8] = { 0 };

	//mdelay(500);

	rc = elan_ktf2k_ts_poll(client);
	if (rc < 0)
	{
		printk( "[elan] %s: Int poll failed!\n", __func__);
		RECOVERY = 0x80;
		return RECOVERY;
		//return -EINVAL;
	}

	do
	{
		rc = i2c_master_recv(client, buf_recv, 8);
		if (rc < 0)
			printk("[elan] %s: read failed rc=%d\n", __func__, rc);
	}while(rc < 0 && --retry > 0);
	printk("[elan] %s: hello packet %2x:%2X:%2x:%2x:%2x:%2x:%2x:%2x\n", __func__, buf_recv[0], buf_recv[1], buf_recv[2], buf_recv[3] , buf_recv[4], buf_recv[5], buf_recv[6], buf_recv[7]);

	//__fw_packet_handler(client);
		
	if(buf_recv[0]==0x55 && buf_recv[1]==0x55 && buf_recv[2]==0x80 && buf_recv[3]==0x80)
	{
		RECOVERY = 0x80;
		return RECOVERY;
	}
	return (rc < 0)? rc : 0;
}

static int __fw_packet_handler(struct i2c_client *client)
{
	struct elan_ktf2k_ts_data *ts = i2c_get_clientdata(client);
	int rc;
	int major, minor;
	uint8_t cmd[]   = {CMD_R_PKT, 0x00, 0x00, 0x01}; /*Get firmware version*/
	uint8_t cmd_x[] = {CMD_R_PKT, 0x60, 0x00, 0x00}; /*Get x resolution*/
	uint8_t cmd_y[] = {CMD_R_PKT, 0x63, 0x00, 0x00}; /*Get y resolution*/
	uint8_t cmd_id[] = {CMD_R_PKT, 0xf0, 0x00, 0x01}; /*Get firmware ID*/
	//uint8_t cmd_bc[] = {CMD_R_PKT, 0x01, 0x00, 0x01};/* Get BootCode Version*/
	uint8_t buf_recv[4] = {0};

// Firmware version
	rc = elan_ktf2k_ts_get_data(client, cmd, buf_recv, 4);
	if (rc < 0)
		return rc;
	major = ((buf_recv[1] & 0x0f) << 4) | ((buf_recv[2] & 0xf0) >> 4);
	minor = ((buf_recv[2] & 0x0f) << 4) | ((buf_recv[3] & 0xf0) >> 4);
	ts->fw_ver = major << 8 | minor;
	//FW_VERSION = ts->fw_ver;
// Firmware ID
	rc = elan_ktf2k_ts_get_data(client, cmd_id, buf_recv, 4);
	if (rc < 0)
		return rc;
	major = ((buf_recv[1] & 0x0f) << 4) | ((buf_recv[2] & 0xf0) >> 4);
	minor = ((buf_recv[2] & 0x0f) << 4) | ((buf_recv[3] & 0xf0) >> 4);
	ts->fw_id = major << 8 | minor;
	//FW_ID = ts->fw_id;
// X Resolution
	rc = elan_ktf2k_ts_get_data(client, cmd_x, buf_recv, 4);
	if (rc < 0)
		return rc;
	minor = ((buf_recv[2])) | ((buf_recv[3] & 0xf0) << 4);
	ts->x_resolution = minor;
	//X_RESOLUTION = ts->x_resolution;

// Y Resolution	
	rc = elan_ktf2k_ts_get_data(client, cmd_y, buf_recv, 4);
	if (rc < 0)
		return rc;
	minor = ((buf_recv[2])) | ((buf_recv[3] & 0xf0) << 4);
	ts->y_resolution = minor;
	//Y_RESOLUTION = ts->y_resolution;

	printk(KERN_INFO "[elan] %s: firmware version: 0x%4.4x\n",
			__func__, ts->fw_ver);
	printk(KERN_INFO "[elan] %s: firmware ID: 0x%4.4x\n",
			__func__, ts->fw_id);
	printk(KERN_INFO "[elan] %s: x resolution: %d, y resolution: %d\n",
			__func__, ts->x_resolution, ts->y_resolution);

	return 0;
}

static inline int elan_ktf2k_ts_parse_xy(uint8_t *data,
			uint16_t *x, uint16_t *y)
{
	*x = *y = 0;

	*x = (data[0] & 0xf0);
	*x <<= 4;
	*x |= data[1];

	*y = (data[0] & 0x0f);
	*y <<= 8;
	*y |= data[2];

	return 0;
}

static int elan_ktf2k_ts_setup(struct i2c_client *client)
{
	int rc;
	struct elan_ktf2k_ts_data *ts = i2c_get_clientdata(client);

//retry:
	rc = __hello_packet_handler(client);
	printk("[elan] hellopacket's rc = %d\n",rc);

	mdelay(10);
	if (rc != 0x80)
	{
	    rc = __fw_packet_handler(client);
	    if (rc < 0)
		    printk("[elan] %s, fw_packet_handler fail, rc = %d", __func__, rc);
	    dev_dbg(&client->dev, "[elan] %s: firmware checking done.\n", __func__);
//Check for FW_VERSION, if 0x0000 means FW update fail!
		if ( ts->fw_ver/*FW_VERSION*/ == 0x00)
		{
			rc = 0x80;
			printk("[elan] FW_VERSION = %d, last FW update fail\n", ts->fw_ver/*FW_VERSION*/);
		}
	}
	return rc;
}

static int elan_ktf2k_ts_rough_calibrate(struct i2c_client *client)
{
	uint8_t cmd[] = {CMD_W_PKT, 0x29, 0x00, 0x01};

	//dev_info(&client->dev, "[elan] %s: enter\n", __func__);
	printk("[elan] %s: enter\n", __func__);
	dev_info(&client->dev,
		"[elan] dump cmd: %02x, %02x, %02x, %02x\n",
		cmd[0], cmd[1], cmd[2], cmd[3]);

	if ((i2c_master_send(client, cmd, sizeof(cmd))) != sizeof(cmd))
	{
		dev_err(&client->dev,
			"[elan] %s: i2c_master_send failed\n", __func__);
		return -EINVAL;
	}

	return 0;
}

static int elan_ktf2k_ts_set_power_state(struct i2c_client *client, int state)
{
	uint8_t cmd[] = {CMD_W_PKT, 0x50, 0x00, 0x01};

	dev_dbg(&client->dev, "[elan] %s: enter\n", __func__);

	cmd[1] |= (state << 3);

	dev_dbg(&client->dev,
		"[elan] dump cmd: %02x, %02x, %02x, %02x\n",
		cmd[0], cmd[1], cmd[2], cmd[3]);

	if ((i2c_master_send(client, cmd, sizeof(cmd))) != sizeof(cmd))
	{
		dev_err(&client->dev,
			"[elan] %s: i2c_master_send failed\n", __func__);
		return -EINVAL;
	}

	return 0;
}

static int elan_ktf2k_ts_get_power_state(struct i2c_client *client)
{
	int rc = 0;
	uint8_t cmd[] = {CMD_R_PKT, 0x50, 0x00, 0x01};
	uint8_t buf[4], power_state;

	rc = elan_ktf2k_ts_get_data(client, cmd, buf, 4);
	if (rc)
		return rc;

	power_state = buf[1];
	dev_dbg(&client->dev, "[elan] dump repsponse: %0x\n", power_state);
	power_state = (power_state & PWR_STATE_MASK) >> 3;
	dev_dbg(&client->dev, "[elan] power state = %s\n",
		power_state == PWR_STATE_DEEP_SLEEP ?
		"Deep Sleep" : "Normal/Idle");

	return power_state;
}

static int elan_ktf2k_ts_recv_data(struct i2c_client *client, uint8_t *buf)
{

	int rc, bytes_to_recv=PACKET_SIZE;
	if (buf == NULL)
		return -EINVAL;

	memset(buf, 0, bytes_to_recv);

	rc = i2c_master_recv(client, buf, 18);
	if (rc != 18)
		 printk("[elan] The first package error.\n");

	//printk("[elan_debug] %x %x %x %x %x %x %x %x\n", 
	//	buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);
	mdelay(1);

	return rc;
}

static void elan_ktf2k_ts_report_data(struct i2c_client *client, uint8_t *buf)
{
	struct elan_ktf2k_ts_data *ts = i2c_get_clientdata(client);
	struct input_dev *idev = ts->input_dev;
	uint16_t x, y;
	uint16_t fbits=0;
	uint8_t i, num, reported = 0;
	uint8_t idx;
	int finger_num;

	// for 5 fingers	
	if ((buf[0] == NORMAL_PKT) || (buf[0] == FIVE_FINGERS_PKT))
	{
		finger_num = 5;
		num = buf[1] & 0x07; 
		fbits = buf[1] >>3;
		idx=2;
	}
	else
	{
		// for 2 fingers      
		finger_num = 2;
		num = buf[7] & 0x03; 
		fbits = buf[7] & 0x03;
		idx=1;
	}

	switch (buf[0])
	{
		case NORMAL_PKT:
		case TWO_FINGERS_PKT:
		case FIVE_FINGERS_PKT:	
		case TEN_FINGERS_PKT:

			if (num == 0)
			{
				dev_dbg(&client->dev, "no press\n");
#if SOFTKEY_AXIS_VER
				if(key_pressed < 0)
				{
					input_report_abs(idev, ABS_MT_TOUCH_MAJOR, 0);
					input_report_key(idev, ABS_MT_WIDTH_MAJOR, 0);
					input_report_key(idev,BTN_TOUCH,0);		
					input_mt_sync(idev);
				}
				else
				{
					dev_err(&client->dev, "[elan] KEY_RELEASE: key_code:%d\n",OSD_mapping[key_pressed].key_event);
					input_report_key(idev, OSD_mapping[key_pressed].key_event, 0);
					key_pressed = -1;
				}
#endif
			}
			else // else if (num != 0)
			{

				dev_dbg(&client->dev, "[elan] %d fingers\n", num);

				for (i = 0; i < finger_num; i++)
				{   
					if ((fbits & 0x01))
					{
						elan_ktf2k_ts_parse_xy(&buf[idx], &x, &y);  
						//elan_ktf2k_ts_parse_xy(&buf[idx], &y, &x);  		 
						//x = ts->x_resolution - x; //X_RESOLUTION-x;
						//y = ts->y_resolution - y; //Y_RESOLUTION-y;
						//printk("[elan_debug] %s, x=%d, y=%d,width = %d,height = %d\n",
						//	__func__, x , y, ts->x_resolution, ts->y_resolution);
						if (!((x <= 0) || ( y<= 0) 
							|| (x >= ts->x_resolution) || (y >= ts->y_resolution)))
						{
							if (y < y_limit)
							{
								if(reported == 0)
									input_report_key(idev, BTN_TOUCH, 1);
								input_report_abs(idev, ABS_MT_TRACKING_ID, i);
								input_report_abs(idev, ABS_MT_TOUCH_MAJOR, 8);
								input_report_abs(idev, ABS_MT_POSITION_X, x);
								input_report_abs(idev, ABS_MT_POSITION_Y, y);
								input_mt_sync(idev);
								reported++;
							}
#if SOFTKEY_AXIS_VER
							else // else if (y >= y_limit)
							{
								int i=0;
								for(i=0;i<3;i++)
								{
									if((x > OSD_mapping[i].left_x) && (x < OSD_mapping[i].right_x))
									{
										dev_err(&client->dev, "[elan] KEY_PRESS: key_code:%d\n",OSD_mapping[i].key_event);

										printk("[elan] %d KEY_PRESS: key_code:%d\n", i, OSD_mapping[i].key_event);

										input_report_key(idev, OSD_mapping[i].key_event, 1);
										//input_report_key(idev, OSD_mapping[i].key_event, 0);
										key_pressed = i;
									}
								}
							}
#endif
						} // end if border
					} // end if finger status
					fbits = fbits >> 1;
					idx += 3;
				} // end for
			}// end if (num == 0)
			if (reported)
				input_sync(idev);
			else
			{
				input_mt_sync(idev);
				input_sync(idev);
			}
			break;
		default:
			dev_err(&client->dev,
						"[elan] %s: unknown packet type: %0x\n", __func__, buf[0]);
			break;
	} // end switch

	return;
}

static void elan_ktf2k_ts_work_func(struct work_struct *work)
{
	int rc;
	struct elan_ktf2k_ts_data *ts =
		container_of(work, struct elan_ktf2k_ts_data, work);
	uint8_t buf[PACKET_SIZE] = { 0 };

	if (gpio_get_value(ts->intr_gpio))
	{
		enable_irq(ts->client->irq);
		return;
	}

	rc = elan_ktf2k_ts_recv_data(ts->client, buf);

	if (rc < 0)
	{
		enable_irq(ts->client->irq);
		return;
	}

	//printk("[elan_debug] %2x,%2x,%2x,%2x,%2x,%2x\n",buf[0],buf[1],buf[2],buf[3],buf[5],buf[6]);
	elan_ktf2k_ts_report_data(ts->client, buf);
	enable_irq(ts->client->irq);

	return;
}

static irqreturn_t elan_ktf2k_ts_irq_handler(int irq, void *dev_id)
{
	struct elan_ktf2k_ts_data *ts = dev_id;
	struct i2c_client *client = ts->client;

	dev_dbg(&client->dev, "[elan] %s\n", __func__);
	disable_irq_nosync(ts->client->irq);
	queue_work(ts->elan_wq, &ts->work);

	return IRQ_HANDLED;
}

static int elan_ktf2k_ts_register_interrupt(struct i2c_client *client)
{
	struct elan_ktf2k_ts_data *ts = i2c_get_clientdata(client);
	int err = 0;

	err = request_irq(client->irq, elan_ktf2k_ts_irq_handler,
			IRQF_TRIGGER_FALLING/*IRQF_TRIGGER_LOW*/, client->name, ts);
	if (err)
		dev_err(&client->dev, "[elan] %s: request_irq %d failed\n",
				__func__, client->irq);

	return err;
}

static int elan_ktf2k_ts_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int err = 0;
	int fw_err = 0;
	//int i;
	#if IAP_PORTION
	int New_FW_ID;	
	int New_FW_VER;
	#endif

	struct elan_ktf2k_i2c_platform_data *pdata;
	struct elan_ktf2k_ts_data *ts;

	//X_RESOLUTION=320;
	//Y_RESOLUTION=640;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
	{
		printk(KERN_ERR "[elan] %s: i2c check functionality error\n", __func__);
		err = -ENODEV;
		goto err_check_functionality_failed;
	}

	ts = kzalloc(sizeof(struct elan_ktf2k_ts_data), GFP_KERNEL);
	if (ts == NULL)
	{
		printk(KERN_ERR "[elan] %s: allocate elan_ktf2k_ts_data failed\n", __func__);
		err = -ENOMEM;
		goto err_alloc_data_failed;
	}

	ts->elan_wq = create_singlethread_workqueue("elan_wq");
	if (!ts->elan_wq)
	{
		printk(KERN_ERR "[elan] %s: create workqueue failed\n", __func__);
		err = -ENOMEM;
		goto err_create_wq_failed;
	}

	INIT_WORK(&ts->work, elan_ktf2k_ts_work_func);
	ts->client = client;
	i2c_set_clientdata(client, ts);

// james: maybe remove	
	pdata = client->dev.platform_data;
	if (likely(pdata != NULL))
	{
		ts->intr_gpio = pdata->intr_gpio;
	}

	//elan_ktf2k_ts_reset();
	//if (pdata->power)
	//	ts->power = pdata->power;
	ts->power = ektf2k_touchscreen_power;
	if (ts->power)
		ts->power(1);

	ts->fw_id  = 0;
	ts->fw_ver = 0;
	ts->x_resolution = 0;
	ts->y_resolution = 0;

	private_ts = ts;

	fw_err = elan_ktf2k_ts_setup(client);
	if (fw_err /*&& fw_err == 0x80*/)
	{
		printk(KERN_INFO "[elan] No Elan chip inside\n");
		goto err_power_enable;
	}

	//y abs max
	#if SOFTKEY_AXIS_VER
    y_limit = ts->y_resolution - 95;
	#else
	y_limit = ts->y_resolution;
	#endif

	ts->input_dev = input_allocate_device();
	if (ts->input_dev == NULL)
	{
		err = -ENOMEM;
		dev_err(&client->dev, "[elan] Failed to allocate input device\n");
		goto err_input_dev_alloc_failed;
	}
	ts->input_dev->name = "elan-touchscreen";   // for andorid2.2 Froyo  

	set_bit(BTN_TOUCH, ts->input_dev->keybit);
	
	input_set_abs_params(ts->input_dev, ABS_X, 0, ts->x_resolution, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_Y, 0, y_limit, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_PRESSURE, 0, 255, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_TOOL_WIDTH, 0, 255, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X, 0, ts->x_resolution, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y, 0, y_limit, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_TRACKING_ID, 0, 5, 0, 0);	

	__set_bit(EV_ABS, ts->input_dev->evbit);
	__set_bit(EV_SYN, ts->input_dev->evbit);
	__set_bit(EV_KEY, ts->input_dev->evbit);
	#if SOFTKEY_AXIS_VER
	//set_bit(EV_KEY, ts->input_dev->evbit);
	//set_bit(EV_ABS, ts->input_dev->evbit);
	set_bit(KEY_MENU, ts->input_dev->keybit);
	set_bit(KEY_HOME, ts->input_dev->keybit);
	set_bit(KEY_BACK, ts->input_dev->keybit);
	#endif

// james: check input_register_devices & input_set_abs_params sequence
	err = input_register_device(ts->input_dev);
	if (err)
	{
		dev_err(&client->dev,
			"[elan]%s: unable to register %s input device\n",
			__func__, ts->input_dev->name);
		goto err_input_register_device_failed;
	}

	elan_ktf2k_ts_register_interrupt(ts->client);

	if (gpio_get_value(ts->intr_gpio) == 0)
	{
		printk(KERN_INFO "[elan]%s: handle missed interrupt\n", __func__);
		elan_ktf2k_ts_irq_handler(client->irq, ts);
	}

// james: check earlysuspend
#ifdef CONFIG_HAS_EARLYSUSPEND
	ts->early_suspend.level = EARLY_SUSPEND_LEVEL_STOP_DRAWING - 1;
	ts->early_suspend.suspend = elan_ktf2k_ts_early_suspend;
	ts->early_suspend.resume = elan_ktf2k_ts_late_resume;
	register_early_suspend(&ts->early_suspend);
#endif

	elan_ktf2k_touch_sysfs_init(ts->input_dev);

	dev_info(&client->dev, "[elan] Start touchscreen %s in interrupt mode\n",
		ts->input_dev->name);

// Firmware Update
	ts->firmware.minor = MISC_DYNAMIC_MINOR;
	ts->firmware.name = "elan-iap";
	ts->firmware.fops = &elan_touch_fops;
	ts->firmware.mode = S_IFREG|S_IRWXUGO; 

	if (misc_register(&ts->firmware) < 0)
		printk("[ELAN]misc_register failed!!");
	else
		printk("[ELAN]misc_register finished!!");
// End Firmware Update

#if IAP_PORTION
	if(0/*RECOVERY == 0x80*/)
	{
		//printk("[ELAN]misc_register finished!!");
		work_lock=1;
		disable_irq(ts->client->irq);
		cancel_work_sync(&ts->work);
	
		power_lock = 1;
/* FW VER & ID*/
		printk("elan [7bd0]=0x%02x,[7bd1]=0x%02x,[7bd2]=0x%02x,[7bd3]=0x%02x\n",file_fw_data[0x7bd0],file_fw_data[0x7bd1],file_fw_data[0x7bd2],file_fw_data[0x7bd3]);
		New_FW_ID  = file_fw_data[0x7bd3]<<8  | file_fw_data[0x7bd2] ;	       
		New_FW_VER = file_fw_data[0x7bd1]<<8  | file_fw_data[0x7bd0] ;

		printk("elan FW_ID=0x%x, New_FW_ID=0x%x \n", ts->fw_id/*FW_ID*/, New_FW_ID);   	       
		printk("elan FW_VERSION=0x%x,New_FW_VER=0x%x \n", ts->fw_ver/*FW_VERSION*/,New_FW_VER);  

/* for firmware auto-upgrade */  
         
		if (New_FW_ID == ts->fw_id/*FW_ID*/)
		{

			printk("elan FW ID same\n");  
			if ((New_FW_VER &0x00ff) != (ts->fw_ver/*FW_VERSION*/ &0x00ff))
			{

				printk("elan FW_VERSION no same\n");
				//Update_FW_One(client, RECOVERY);

			}
		}
		else
		{
			printk("FW_ID is different!");
			//Update_FW_One(client, RECOVERY);
		}


		power_lock = 0;
		work_lock=0;
		enable_irq(ts->client->irq);
		printk("elan start iap update end\n");
	}
#endif

	return 0;

err_input_register_device_failed:
	if (ts->input_dev)
		input_free_device(ts->input_dev);
err_power_enable:
	if (ts->power)
		ts->power(0);
err_input_dev_alloc_failed: 
	if (ts->elan_wq)
		destroy_workqueue(ts->elan_wq);

err_create_wq_failed:
	kfree(ts);

err_alloc_data_failed:
err_check_functionality_failed:

	return err;
}

static int elan_ktf2k_ts_remove(struct i2c_client *client)
{
	struct elan_ktf2k_ts_data *ts = i2c_get_clientdata(client);
	if (ts->power)
	{
		ts->power(0);
	}
	elan_touch_sysfs_deinit();

	unregister_early_suspend(&ts->early_suspend);
	free_irq(client->irq, ts);

	if (ts->elan_wq)
		destroy_workqueue(ts->elan_wq);
	input_unregister_device(ts->input_dev);
	kfree(ts);

	return 0;
}

static int elan_ktf2k_ts_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct elan_ktf2k_ts_data *ts = i2c_get_clientdata(client);
	int rc = 0;
	if(power_lock == 0) /* The power_lock can be removed when firmware upgrade procedure will not be enter into suspend mode.  */
	{
		printk(KERN_INFO "[elan] %s: enter\n", __func__);

		disable_irq(client->irq);

		rc = cancel_work_sync(&ts->work);
		if (rc)
			enable_irq(client->irq);

		rc = elan_ktf2k_ts_set_power_state(client, PWR_STATE_DEEP_SLEEP);
	}
	return 0;
}

static int elan_ktf2k_ts_resume(struct i2c_client *client)
{
	int rc = 0, retry = 5;
	if(power_lock == 0)   /* The power_lock can be removed when firmware upgrade procedure will not be enter into suspend mode.  */
	{
		printk(KERN_INFO "[elan] %s: enter\n", __func__);

		do
		{
			rc = elan_ktf2k_ts_set_power_state(client, PWR_STATE_NORMAL);
			rc = elan_ktf2k_ts_get_power_state(client);
			if (rc != PWR_STATE_NORMAL)
				printk(KERN_ERR "[elan] %s: wake up tp failed! err = %d\n",
					__func__, rc);
			else
				break;
		} while (--retry);

		enable_irq(client->irq);
	}
	return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void elan_ktf2k_ts_early_suspend(struct early_suspend *h)
{
	struct elan_ktf2k_ts_data *ts;
	ts = container_of(h, struct elan_ktf2k_ts_data, early_suspend);
	elan_ktf2k_ts_suspend(ts->client, PMSG_SUSPEND);
}

static void elan_ktf2k_ts_late_resume(struct early_suspend *h)
{
	struct elan_ktf2k_ts_data *ts;
	ts = container_of(h, struct elan_ktf2k_ts_data, early_suspend);
	elan_ktf2k_ts_resume(ts->client);
}
#endif

static const struct i2c_device_id elan_ktf2k_ts_id[] = {
	{ ELAN_KTF2K_NAME, 0 },
	{ }
};

static struct i2c_driver ektf2k_ts_driver = {
	.probe		= elan_ktf2k_ts_probe,
	.remove		= elan_ktf2k_ts_remove,
#ifndef CONFIG_HAS_EARLYSUSPEND
	.suspend	= elan_ktf2k_ts_suspend,
	.resume		= elan_ktf2k_ts_resume,
#endif
	.id_table	= elan_ktf2k_ts_id,
	.driver		= {
		.name = ELAN_KTF2K_NAME,
	},
};

extern int get_pcb_version(void);

static int __devinit elan_ktf2k_ts_init(void)
{
	if (get_pcb_version() < 20)
		return -EINVAL;
	printk(KERN_INFO "[elan] %s driver version 0x0003: Integrated 2 and 5 fingers together and auto-mapping resolution\n", __func__);
	return i2c_add_driver(&ektf2k_ts_driver);
}

static void __exit elan_ktf2k_ts_exit(void)
{
	i2c_del_driver(&ektf2k_ts_driver);
	return;
}

module_init(elan_ktf2k_ts_init);
module_exit(elan_ktf2k_ts_exit);

MODULE_DESCRIPTION("ELAN KTF2K Touchscreen Driver");
MODULE_LICENSE("GPL");


