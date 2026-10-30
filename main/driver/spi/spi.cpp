#include "spi.hpp"

#include <algorithm>

#include <string.h>
#include <esp_log.h>
#include <esp_task.h>

constexpr TickType_t dontWait = 0;

SPI::SPI(SpiHost host, GPIO MISO, GPIO MOSI, GPIO CLOCK) :
	host{ host }
{
	spi_bus_config_t buscfg{};
	buscfg.miso_io_num = MISO;
	buscfg.mosi_io_num = MOSI;
	buscfg.sclk_io_num = CLOCK;
	buscfg.quadwp_io_num = GPIO::NC;
	buscfg.quadhd_io_num = GPIO::NC;
	buscfg.max_transfer_sz = 1 << 15; // dma 限制

	auto ret = spi_bus_initialize(host, &buscfg, SPI_DMA_CH_AUTO);
	ESP_ERROR_CHECK(ret);
}

SPI::SPI(SPI&& move)
{
	std::swap(move.host, host);
}

SPI& SPI::operator=(SPI&& move)
{
	std::swap(move.host, host);
	return *this;
}

SPI::~SPI()
{
	if (host != SPI_HOST_MAX)
	{
		auto ret = spi_bus_free(host);
		ESP_ERROR_CHECK(ret);
		host = SpiHost::SPI_HOST_MAX;
	}
}

SPIDevice::SPIDevice(SPI& host, GPIO CS, unsigned char transmitionSize, int speed) :
	transmitionSize{ transmitionSize }
{
	transmitionIndex = 0;
	transmition = new Transmition[transmitionSize];
	for (unsigned i = 0; i < transmitionSize; i++)
	{
		transmition[i].transmition = {};
		transmition[i].transmitting = false;

		transmition[i].spiDevice = this;
		transmition[i].callbackBefore = emptyCallback;
		transmition[i].callbackBeforeData = nullptr;
	}
	transmitionCount = xSemaphoreCreateCounting(transmitionSize, transmitionSize);

	spi_device_interface_config_t devcfg = {};
	devcfg.clock_speed_hz = speed;
	devcfg.mode = 0;
	devcfg.spics_io_num = CS;
	devcfg.queue_size = transmitionSize;
	devcfg.pre_cb = spiDeviceStartCallback;
	devcfg.post_cb = spiDeviceFinishCallback;
	devcfg.flags = SPI_DEVICE_NO_RETURN_RESULT;

	auto ret = spi_bus_add_device(host, &devcfg, &device);
	ESP_ERROR_CHECK(ret);
}

SPIDevice::SPIDevice(SPIDevice&& move)
{
	using std::swap;
	swap(move.device, device);
	swap(move.transmition, transmition);
	swap(move.transmitionSize, transmitionSize);
	swap(move.transmitionIndex, transmitionIndex);
	swap(move.transmitionCount, transmitionCount);
}

SPIDevice& SPIDevice::operator=(SPIDevice&& move)
{
	using std::swap;
	swap(move.device, device);
	swap(move.transmition, transmition);
	swap(move.transmitionSize, transmitionSize);
	swap(move.transmitionIndex, transmitionIndex);
	swap(move.transmitionCount, transmitionCount);

	for (unsigned char i = 0; i < transmitionSize; i++)
		transmition[i].spiDevice = this;
	for (unsigned char i = 0; i < move.transmitionSize; i++)
		move.transmition[i].spiDevice = &move;

	return *this;
}

SPIDevice::~SPIDevice()
{
	if (device != nullptr)
	{
		auto ret = spi_bus_remove_device(device);
		ESP_ERROR_CHECK(ret);
	}

	delete[] transmition;
	transmitionSize = 0;
	transmitionIndex = 0;
	if (transmitionCount != nullptr)
	{
		vSemaphoreDelete(transmitionCount);
		transmitionCount = nullptr;
	}
}

bool SPIDevice::transmit(const void* data, size_t sizeInBit, function_t callbackBefore, void* callbackBeforeData, function_t callbackAfter, void* callbackAfterData)
{
	auto& nowTransmition = transmition[transmitionIndex];

	if (nowTransmition.transmitting) [[unlikely]]
	{
		ESP_LOGW("SPIDevice", "transmition all unaviliable! transmiting count = %d", uxSemaphoreGetCount(transmitionCount));
		return false;
	}

	transmitionIndex++;
	if (transmitionIndex >= transmitionSize)
		transmitionIndex = 0;

	nowTransmition.callbackBefore = callbackBefore;
	nowTransmition.callbackAfter = callbackAfter;
	nowTransmition.callbackBeforeData = callbackBeforeData;
	nowTransmition.callbackAfterData = callbackAfterData;

	auto& transmitting = nowTransmition.transmitting;
	auto& espTransmition = nowTransmition.transmition;

	transmitting = true;
	xSemaphoreTake(transmitionCount, portMAX_DELAY);

	espTransmition.length = sizeInBit;
	espTransmition.rxlength = 0;
	espTransmition.tx_buffer = data;
	espTransmition.flags &= ~SPI_TRANS_USE_TXDATA;
	espTransmition.user = &nowTransmition;

	auto ret = spi_device_queue_trans(device, &espTransmition, dontWait);
	if (ret == ESP_OK) [[likely]] return true;

	// failed
	transmitting = false;
	xSemaphoreGive(transmitionCount);
	return false;
}

bool SPIDevice::transmit(SmallData_t data, size_t sizeInBit, function_t callbackBefore, void* callbackBeforeData, function_t callbackAfter, void* callbackAfterData)
{
	auto& nowTransmition = transmition[transmitionIndex];

	if (nowTransmition.transmitting) [[unlikely]]
	{
		ESP_LOGW("SPIDevice", "transmition all unaviliable!");
		return false;
	}

	transmitionIndex++;
	if (transmitionIndex >= transmitionSize)
		transmitionIndex = 0;

	nowTransmition.callbackBefore = callbackBefore;
	nowTransmition.callbackAfter = callbackAfter;
	nowTransmition.callbackBeforeData = callbackBeforeData;
	nowTransmition.callbackAfterData = callbackAfterData;

	auto& transmitting = nowTransmition.transmitting;
	auto& espTransmition = nowTransmition.transmition;

	transmitting = true;
	xSemaphoreTake(transmitionCount, portMAX_DELAY);

	espTransmition.length = sizeInBit;
	espTransmition.rxlength = 0;
	espTransmition.tx_data[0] = data.data[0];
	espTransmition.tx_data[1] = data.data[1];
	espTransmition.tx_data[2] = data.data[2];
	espTransmition.tx_data[3] = data.data[3];
	espTransmition.flags |= SPI_TRANS_USE_TXDATA;
	espTransmition.user = &nowTransmition;

	auto ret = spi_device_queue_trans(device, &espTransmition, dontWait);
	// ESP_ERROR_CHECK(ret);
	if (ret == ESP_OK) [[likely]] return true;

	ESP_LOGE("SPIDevice", "error %d %s", ret, esp_err_to_name(ret));

	vTaskDelay(1);

	// failed
	transmitting = false;
	xSemaphoreGive(transmitionCount);
	return false;
}

unsigned char SPIDevice::getTransmittingCount()
{
	return transmitionSize - uxSemaphoreGetCount(transmitionCount);
}

void SPIDevice::waitForTransmition(waitFunction_t waitFunction)
{
	while (getTransmittingCount() != 0 && waitFunction()) {}
}

void SPI_IRAM SPIDevice::spiDeviceStartCallback(spi_transaction_t* trans)
{
	auto& myTransmition = *(SPIDevice::Transmition*)trans->user;
	auto& callBack = myTransmition.callbackBefore;
	auto& param = myTransmition.callbackBeforeData;

	callBack(param);
}

void SPI_IRAM SPIDevice::spiDeviceFinishCallback(spi_transaction_t* trans)
{
	auto& myTransmition = *(SPIDevice::Transmition*)trans->user;
	auto& callBack = myTransmition.callbackAfter;
	auto& param = myTransmition.callbackAfterData;

	myTransmition.transmitting = false;
	xSemaphoreGiveFromISR(myTransmition.spiDevice->transmitionCount, nullptr);
	callBack(param);
}
