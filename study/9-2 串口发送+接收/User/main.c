#include "stm32f10x.h"                  // Device header
#include "Delay.h"
#include "OLED.h"
#include "Serial.h"
#include "RB.h"
#include "App.h"
#include "W25Q64.h"

#define LINE_BUFFER_SIZE 32

/*
第四阶段：DMA循环接收进度
1. 拆分阶段小步，确认DMA要替代RXNE中断搬运：已完成
2. 定义USART DMA循环接收缓冲区和扫描位置：已完成
3. 配置DMA1_Channel5从USART1->DR搬运到接收缓冲区：已完成
4. 开启USART1的DMA接收请求，暂时保留原有主循环处理逻辑：已完成
5. 主循环扫描DMA写入的新字节，并写入RB环形缓冲区：已完成
6. 关闭USART1 RXNE接收中断，让DMA正式接管接收搬运：已完成
7. 阶段测试与整理：已整理，待板上验证

第五阶段：W25Q64 Flash 写入
1. 加入 W25Q64 驱动文件并初始化：已完成
2. 测试 READID 验证硬件连接：已完成
3. 串口接收数据写入 Flash：未开始
   - 3.1 定义页缓冲区、页索引、Flash地址变量：已完成
   - 3.2 数据字节写入页缓冲区，满页自动写入 Flash：未开始
   - 3.3 WRITE:size 时擦除扇区，收完 flush 剩余数据：未开始
4. 读回验证：未开始
*/

uint8_t RxData;			//定义用于接收串口数据的变量
char line_buffer[LINE_BUFFER_SIZE];
uint8_t line_index = 0;
uint8_t ignore_next_lf = 0;


int main(void)
{
	/*模块初始化*/
	OLED_Init();		//OLED初始化
	
	/*显示静态字符串*/
	OLED_ShowString(1, 1, "RxData:");
	OLED_ShowString(2, 1, "Count:");
	OLED_ShowString(3, 1, "Ovf:");
	OLED_ShowString(4, 1, "Line:");
	
	/*串口初始化*/
	Serial_Init();		//串口初始化
	
	/*W25Q64初始化*/
	W25Q64_Init();		//初始化SPI和W25Q64
	
	while (1)
	{
		uint8_t data;
		
		Serial_ProcessDMARx();								//扫描DMA接收缓冲区，把新字节写入RB
		
		if (RB_Read(&data))
		{
			if (App_GetState() == APP_STATE_RECEIVING)
			{
				if (ignore_next_lf && data == '\n')	//防止“\n”被当作字符数据处理
				{
					ignore_next_lf = 0;				//忽略WRITE命令后CRLF中的LF，避免把它算作数据
				}
				else
				{
					ignore_next_lf = 0;
					App_HandleDataByte(data);		//数据接收状态下，字节不再进入命令行缓冲区
				}
			}
			else if (data == '\n' || data == '\r')		//传输完毕
			{
				if (line_index > 0)
				{
					line_buffer[line_index] = '\0';		//补字符串结束符，形成完整字符串
					App_HandleLine(line_buffer);			//处理收到的一整行命令
					if (data == '\r' && App_GetState() == APP_STATE_RECEIVING)
					{
						ignore_next_lf = 1;				//串口助手常发送CRLF，下一字节若为LF则不算作数据
					}
					line_index = 0;						//清空行缓冲区下标，准备接收下一行
				}
			}
			else
			{
				if (line_index < LINE_BUFFER_SIZE - 1)
				{
					line_buffer[line_index] = data;		//普通字符先存入行缓冲区
					line_index ++;						//下标后移，指向下一个可写位置
				}
				else
				{
					line_index = 0;						//命令太长时丢弃当前行，重新开始接收
					Serial_SendString("Line too long\r\n");
				}
			}
		}
	}
}
