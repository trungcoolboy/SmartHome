#include "axis_travel_store.h"

#include "main.h"

#define AXIS_TRAVEL_STORE_MAGIC        0x4C565254UL
#define AXIS_TRAVEL_STORE_VERSION      1UL
#define AXIS_TRAVEL_STORE_FLASH_ADDR   0x0801F800UL
#define AXIS_TRAVEL_STORE_PAGE_SIZE    2048UL

typedef struct
{
  uint32_t magic;
  uint32_t version;
  uint32_t a_travel_steps;
  uint32_t b_travel_steps;
  uint32_t checksum;
  uint32_t reserved0;
  uint32_t reserved1;
  uint32_t reserved2;
} AxisTravelStoreRecord;

static uint32_t cached_a_travel_steps = 0U;
static uint32_t cached_b_travel_steps = 0U;
static uint8_t cached_loaded_from_flash = 0U;

static uint32_t axis_travel_store_checksum(const AxisTravelStoreRecord *record)
{
  const uint32_t *words = (const uint32_t *)record;
  uint32_t hash = 2166136261UL;
  size_t i;

  for (i = 0U; i < 4U; i++)
  {
    uint32_t value = words[i];
    uint8_t byte_index;
    for (byte_index = 0U; byte_index < 4U; byte_index++)
    {
      hash ^= (value & 0xFFU);
      hash *= 16777619UL;
      value >>= 8U;
    }
  }

  return hash;
}

static uint8_t axis_travel_store_record_valid(const AxisTravelStoreRecord *record)
{
  if (record->magic != AXIS_TRAVEL_STORE_MAGIC || record->version != AXIS_TRAVEL_STORE_VERSION)
  {
    return 0U;
  }

  if (record->a_travel_steps < 100U || record->a_travel_steps > 100000U)
  {
    return 0U;
  }

  if (record->b_travel_steps < 100U || record->b_travel_steps > 100000U)
  {
    return 0U;
  }

  return (axis_travel_store_checksum(record) == record->checksum) ? 1U : 0U;
}

static uint8_t axis_travel_store_write_cache(void)
{
  AxisTravelStoreRecord record = {
    .magic = AXIS_TRAVEL_STORE_MAGIC,
    .version = AXIS_TRAVEL_STORE_VERSION,
    .a_travel_steps = cached_a_travel_steps,
    .b_travel_steps = cached_b_travel_steps,
    .checksum = 0U,
    .reserved0 = 0U,
    .reserved1 = 0U,
    .reserved2 = 0U,
  };
  FLASH_EraseInitTypeDef erase = {0};
  uint32_t page_error = 0U;
  uint32_t address = AXIS_TRAVEL_STORE_FLASH_ADDR;
  const uint64_t *double_words = (const uint64_t *)&record;
  size_t index;

  record.checksum = axis_travel_store_checksum(&record);

  if (HAL_FLASH_Unlock() != HAL_OK)
  {
    return 0U;
  }

  erase.TypeErase = FLASH_TYPEERASE_PAGES;
  erase.Banks = FLASH_BANK_1;
  erase.Page = (AXIS_TRAVEL_STORE_FLASH_ADDR - FLASH_BASE) / AXIS_TRAVEL_STORE_PAGE_SIZE;
  erase.NbPages = 1U;

  if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK)
  {
    HAL_FLASH_Lock();
    return 0U;
  }

  for (index = 0U; index < (sizeof(record) / sizeof(uint64_t)); index++)
  {
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, address, double_words[index]) != HAL_OK)
    {
      HAL_FLASH_Lock();
      return 0U;
    }
    address += sizeof(uint64_t);
  }

  HAL_FLASH_Lock();
  cached_loaded_from_flash = 1U;
  return 1U;
}

void axis_travel_store_init(uint32_t default_a_travel_steps, uint32_t default_b_travel_steps)
{
  const AxisTravelStoreRecord *record = (const AxisTravelStoreRecord *)AXIS_TRAVEL_STORE_FLASH_ADDR;

  cached_a_travel_steps = default_a_travel_steps;
  cached_b_travel_steps = default_b_travel_steps;
  cached_loaded_from_flash = 0U;

  if (axis_travel_store_record_valid(record))
  {
    cached_a_travel_steps = record->a_travel_steps;
    cached_b_travel_steps = record->b_travel_steps;
    cached_loaded_from_flash = 1U;
  }
}

uint8_t axis_travel_store_get(uint32_t *a_travel_steps, uint32_t *b_travel_steps)
{
  if (a_travel_steps != NULL)
  {
    *a_travel_steps = cached_a_travel_steps;
  }
  if (b_travel_steps != NULL)
  {
    *b_travel_steps = cached_b_travel_steps;
  }
  return cached_loaded_from_flash;
}

uint8_t axis_travel_store_save_a(uint32_t a_travel_steps)
{
  if (a_travel_steps < 100U)
  {
    return 0U;
  }
  if (cached_loaded_from_flash != 0U && cached_a_travel_steps == a_travel_steps)
  {
    return 1U;
  }
  cached_a_travel_steps = a_travel_steps;
  return axis_travel_store_write_cache();
}

uint8_t axis_travel_store_save_b(uint32_t b_travel_steps)
{
  if (b_travel_steps < 100U)
  {
    return 0U;
  }
  if (cached_loaded_from_flash != 0U && cached_b_travel_steps == b_travel_steps)
  {
    return 1U;
  }
  cached_b_travel_steps = b_travel_steps;
  return axis_travel_store_write_cache();
}
