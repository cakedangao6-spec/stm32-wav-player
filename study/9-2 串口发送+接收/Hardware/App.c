#include "stm32f10x.h"                  // Device header
#include <string.h>
#include "Serial.h"
#include "App.h"
#include "W25Q64.h"

static uint32_t app_write_size = 0;		//记录WRITE:size命令中解析出的文件大小
static uint32_t app_received_size = 0;	//记录当前已经接收到的数据字节数
static uint8_t app_write_ready = 0;		//记录是否已经收到有效的WRITE:size命令
static App_State_t app_state = APP_STATE_IDLE;	//记录当前处于命令空闲状态还是数据接收状态

#define APP_PAGE_SIZE 256					//W25Q64页大小(256字节)
#define APP_SECTOR_SIZE 4096				//W25Q64扇区大小(4KB)，擦除最小单位
#define APP_FLASH_START_ADDR 0				//数据写入Flash的起始地址
static uint8_t app_page_buf[APP_PAGE_SIZE];	//页缓冲区，攒够256字节写入Flash
static uint16_t app_page_index = 0;			//页缓冲区当前写入位置
static uint32_t app_flash_addr = 0;			//当前写入的Flash地址

/**
  * @brief  获取APP当前接收状态
  * @param  无
  * @retval 当前APP状态
  */
App_State_t App_GetState(void)		//App_State_t 枚举类型
{
	return app_state;
}

/**
  * @brief  获取当前已经接收到的数据字节数
  * @param  无
  * @retval 已接收字节数
  */
uint32_t App_GetReceivedSize(void)
{
	return app_received_size;
}

/**
  * @brief  处理一个原始数据字节
  * @param  data 收到的一个数据字节，存入页缓冲区
  * @retval 无
  * @note   每收到一个字节就写入页缓冲区；攒满256字节时自动调用W25Q64_PageProgram写入Flash。
  *         注意：写Flash时阻塞等待Busy，但DMA仍在后台搬运，不会丢字节。
  */
void App_HandleDataByte(uint8_t data)
{
	if (app_state == APP_STATE_RECEIVING)
	{
		app_page_buf[app_page_index] = data;	//存入页缓冲区
		app_page_index ++;
		app_received_size ++;

		/*页缓冲区已满（256字节），整页写入Flash*/
		if (app_page_index >= APP_PAGE_SIZE)
		{
			W25Q64_PageProgram(app_flash_addr, app_page_buf, APP_PAGE_SIZE);	//写入Flash，内部会等待写入完成
			app_flash_addr += APP_PAGE_SIZE;	//Flash地址后移一页
			app_page_index = 0;					//清空页下标，从头攒下一页
		}

		/*收满WRITE:size指定的总字节数*/
		if (app_received_size >= app_write_size)
		{
			/*flush页缓冲区剩余数据：不满一页也要写入Flash*/
			if (app_page_index > 0)
			{
				W25Q64_PageProgram(app_flash_addr, app_page_buf, app_page_index);
			}
			app_state = APP_STATE_IDLE;
			Serial_SendString("DONE\r\n");
		}
	}
}

/**
  * @brief  解析WRITE命令中的size字段
  * @param  line 已经以'\0'结尾的字符串命令，格式应为WRITE:size
  * @param  size 解析出的文件大小存放地址
  * @retval 解析成功返回1，格式错误返回0
  * @note   当前只解析size，不接收文件数据，后续再进入下载流程。
  */
static uint8_t App_ParseWriteSize(char *line, uint32_t *size)
{
	char *size_string;
	uint32_t value = 0;
	
	if (strncmp(line, "WRITE:", 6) != 0)
	{
		return 0;
	}
	
	size_string = line + 6;			//跳过"WRITE:"，让指针指向size的第一个字符
	if (*size_string == '\0')
	{
		return 0;					//冒号后面没有数字，认为命令格式错误
	}
	
	while (*size_string != '\0')
	{
		if (*size_string < '0' || *size_string > '9')
		{
			return 0;				//size字段只能由十进制数字组成
		}
		
		value = value * 10 + (*size_string - '0');	//把ASCII数字字符转换成数值并累加
		size_string ++;
	}
	
	*size = value;
	return 1;
}

/**
  * @brief  输出当前轻量协议状态
  * @param  无
  * @retval 无
  * @note   用于观察WRITE准备标志、目标大小、已接收大小和当前状态。内部辅助函数，专门用来响应 STATUS 命令。
  */
static void App_ShowStatus(void)
{
	/*READY表示是否已经收到有效的WRITE:size命令*/
	Serial_SendString("READY=");
	Serial_SendNumber(app_write_ready, 1);
	Serial_SendString("\r\n");
	
	/*SIZE表示最近一次WRITE:size保存下来的文件大小*/
	Serial_SendString("SIZE=");
	Serial_Printf("%lu", app_write_size);
	Serial_SendString("\r\n");
	
	/*RECEIVED表示当前数据接收阶段已经统计到的字节数*/
	Serial_SendString("RECEIVED=");
	Serial_Printf("%lu", app_received_size);
	Serial_SendString("\r\n");
	
	/*STATE用于区分当前是在等待命令，还是正在接收原始数据*/
	Serial_SendString("STATE=");
	if (app_state == APP_STATE_RECEIVING)
	{
		Serial_SendString("RECEIVING\r\n");
	}
	else
	{
		Serial_SendString("IDLE\r\n");
	}
}

/**
  * @brief  处理一行串口命令，并根据命令内容返回响应
  * @param  line 已经以'\0'结尾的字符串命令
  * @retval 无
  * @note   当前支持PING、HELP、WRITE:size、VERIFY、STATUS、READID、READBACK，其他命令回复UNKNOWN。
  */
void App_HandleLine(char *line)
{
	uint32_t write_size;
	
	/*先回显收到的完整行，方便在串口助手里观察解析结果*/
	Serial_SendString("Line:");
	Serial_SendString(line);
	Serial_SendString("\r\n");
	
	/*识别PING命令，用于测试命令解析链路是否正常*/
	if (strcmp(line, "PING") == 0)
	{
		Serial_SendString("PONG\r\n");
	}
	else if (strcmp(line, "HELP") == 0)
	{
		/*HELP只返回当前已经支持的命令，便于串口助手快速查看协议入口*/
		Serial_SendString("CMD:\r\n");
		Serial_SendString("PING\r\n");
		Serial_SendString("HELP\r\n");
		Serial_SendString("WRITE:size\r\n");
		Serial_SendString("VERIFY\r\n");
		Serial_SendString("STATUS\r\n");
	}
	else if (App_ParseWriteSize(line, &write_size))
	{
		/*保存本次准备写入的大小*/
		app_write_size = write_size;
		app_received_size = 0;			//清零已接收计数
		app_page_index = 0;				//清空页缓冲区下标
		app_flash_addr = APP_FLASH_START_ADDR;	//从起始地址开始写
		app_write_ready = 1;
		
		/*W25Q64写入前必须擦除扇区(NOR Flash只能1→0，不能0→1)
		  扇区4KB，向下对齐到扇区边界*/
		Serial_SendString("ERASING\r\n");
		W25Q64_SectorErase(app_flash_addr);	//阻塞等待擦除完成
		Serial_SendString("ERASE OK\r\n");
		
		app_state = APP_STATE_RECEIVING;
		/*READY表示已经准备好接收后续原始数据字节*/
		Serial_SendString("READY\r\n");
	}
	else if (strcmp(line, "VERIFY") == 0)
	{
		/*当前还没有接收文件数据，先验证是否已经通过WRITE:size进入准备状态*/
		if (app_write_ready)
		{
			Serial_SendString("OK\r\n");
		}
		else
		{
			Serial_SendString("NO WRITE\r\n");
		}
	}
	else if (strcmp(line, "STATUS") == 0)
	{
		/*STATUS用于观察当前保存的协议状态，方便学习和调试*/
		App_ShowStatus();
	}
	else if (strcmp(line, "READID") == 0)
	{
		/*读取W25Q64的制造商ID和设备ID，验证SPI和Flash硬件连接是否正常*/
		uint8_t MID;
		uint16_t DID;
		W25Q64_ReadID(&MID, &DID);
		Serial_SendString("MID=");
		Serial_Printf("%02X", MID);
		Serial_SendString(", DID=");
		Serial_Printf("%04X", DID);
		Serial_SendString("\r\n");
	}
	else if (strcmp(line, "READBACK") == 0)
	{
		/*从Flash读回刚才写入的数据，通过串口发回PC验证*/
		uint32_t i;
		if (app_write_ready)
		{
			Serial_SendString("DATA:\r\n");
			/*分块读回：每次最多读256字节(页缓冲区大小)*/
			for (i = 0; i < app_write_size; i += APP_PAGE_SIZE)
			{
				uint16_t chunk = app_write_size - i;
				if (chunk > APP_PAGE_SIZE) chunk = APP_PAGE_SIZE;
				W25Q64_ReadData(APP_FLASH_START_ADDR + i, app_page_buf, chunk);
				Serial_SendArray(app_page_buf, chunk);
			}
			Serial_SendString("\r\nREADBACK OK\r\n");
		}
		else
		{
			Serial_SendString("NO DATA\r\n");
		}
	}
	else
	{
		/*未识别的命令统一回复UNKNOWN，保持协议响应简单明确*/
		Serial_SendString("UNKNOWN\r\n");
	}
}
