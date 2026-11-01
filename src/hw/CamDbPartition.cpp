#include "CamDbPartition.h"
#include <esp_partition.h>
#include <string.h>

const uint8_t* camDbMap(size_t* len) {
  const esp_partition_t* p =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x40, "camdb");
  if (!p) return nullptr;
  const void* ptr = nullptr;
  spi_flash_mmap_handle_t handle;
  if (esp_partition_mmap(p, 0, p->size, SPI_FLASH_MMAP_DATA, &ptr, &handle) != ESP_OK) return nullptr;
  const uint8_t* d = (const uint8_t*)ptr;
  if (memcmp(d, "SRDB", 4) != 0) return nullptr;
  uint16_t rows, cols;
  uint32_t count;
  memcpy(&rows, d + 12, 2);
  memcpy(&cols, d + 14, 2);
  memcpy(&count, d + 16, 4);
  size_t need = 32 + ((size_t)rows * cols + 1) * 4 + (size_t)count * 12;
  if (need > p->size) return nullptr;
  *len = need;
  return d;
}
