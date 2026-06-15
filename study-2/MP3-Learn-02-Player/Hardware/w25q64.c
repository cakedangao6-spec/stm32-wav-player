/**
 * @file    w25q64.c
 * @brief   W25Q64 应用层封装，给上层提供更安全、更直观的接口
 */

#include "w25q64.h"
#include "bsp_w25q64.h"

/**
  * 函    数：W25Q64 驱动初始化
  * 参    数：无
  * 返 回 值：无
  */
void W25Q64_DriverInit(void)
{
    W25Q64_Init();                                // 调用底层驱动初始化，主要是释放片选信号
}

/**
  * 函    数：读取 W25Q64 JEDEC ID
  * 参    数：无
  * 返 回 值：24 位 JEDEC ID
  */
uint32_t W25Q64_ReadJedecID(void)
{
    return W25Q64_ReadID();                       // 直接调用底层读 ID 函数
}

/**
  * 函    数：判断芯片是否兼容 W25Q64 容量
  * 参    数：jedec_id 读取到的 JEDEC ID
  * 返 回 值：1 表示容量码匹配 64Mbit，0 表示不匹配
  */
uint8_t W25Q64_Is64MbitCompatible(uint32_t jedec_id)
{
    return ((uint8_t)jedec_id == W25Q64_64MBIT_CAPACITY_CODE) ? 1U : 0U; // JEDEC ID 最低字节是容量编码
}

/**
  * 函    数：带边界检查的 W25Q64 读取
  * 参    数：addr 读取起始地址
  * 参    数：data 接收数据的数组
  * 参    数：len  要读取的长度
  * 返 回 值：0 表示成功，1 表示参数错误或越界
  */
uint8_t W25Q64_Read(uint32_t addr, uint8_t *data, uint32_t len)
{
    if (data == 0) {
        return 1;                                  // 空指针不能读取
    }

    if (len == 0) {
        return 0;                                  // 读取 0 字节直接认为成功
    }

    if (addr > W25Q64_CAPACITY_BYTES || len > (W25Q64_CAPACITY_BYTES - addr)) {
        return 1;                                  // 防止读取范围超过 W25Q64 容量
    }

    W25Q64_ReadData(addr, data, len);              // 参数合法后调用底层连续读取函数
    return 0;
}
