#pragma once

#include "utility"
#include "esp_log.h"
#include "driver/spi/spi.hpp"

class JD7601
{
public:
	constexpr static int Width = 720;
	constexpr static int Height = 480;

	JD7601() = default;
	JD7601(SPI& spiHost, GPIO reset, GPIO dc, GPIO idle, GPIO cs = GPIO::NC, unsigned char transmitionSize = 16, int speed = 2000000) : spi{ spiHost, cs, transmitionSize, speed }, resetGpio{ reset, GPIO::Mode::GPIO_MODE_OUTPUT }, dcGpio{ dc, GPIO::Mode::GPIO_MODE_OUTPUT }, idleGpio{ idle, GPIO::Mode::GPIO_MODE_INPUT } {}
	JD7601(JD7601&& move) { operator=(std::move(move)); }
	JD7601& operator=(JD7601&& move)
	{
		std::swap(move.frame, frame);
		std::swap(move.resetGpio, resetGpio);
		std::swap(move.dcGpio, dcGpio);
		std::swap(move.idleGpio, idleGpio);
		std::swap(move.spi, spi);
		return *this;
	}

	bool reset()
	{
		resetGpio = false;
		vTaskDelay(20);
		resetGpio = true;
		vTaskDelay(10);
		resetGpio = false;
		vTaskDelay(20);
		resetGpio = true;
		vTaskDelay(10);
		return true;
	}

	bool init()
	{
		if (!wait(pdMS_TO_TICKS(5000))) return false;
		if (!command(0xE9)) return false;
		if (!data(0x01)) return false;
		return true;
	}

	bool wait(TickType_t maxWaitTime = pdMS_TO_TICKS(60 * 1000))
	{
		TickType_t timeOut = xTaskGetTickCount() + maxWaitTime;
		while (spi.getTransmittingCount() != 0 && xTaskGetTickCount() < timeOut)
			vTaskDelay(1);
		while ((bool)idleGpio == false && xTaskGetTickCount() < timeOut)
			vTaskDelay(1);
		if (spi.getTransmittingCount() != 0)
			ESP_LOGE(TAG, "still transmiting");
		if ((bool)idleGpio == false)
			ESP_LOGE(TAG, "still busy");
		return spi.getTransmittingCount() == 0 && (bool)idleGpio;
	}

	bool flush()
	{
		if (!wait(pdMS_TO_TICKS(60 * 1000))) return false;
		if (!command(0x10)) return false;

		constexpr int batchCount = 6;
		constexpr size_t batchSize = sizeof(frame->data) / batchCount;
		static_assert(batchSize * batchCount == sizeof(frame->data));
		uint8_t* frameData = &frame->data[0][0];

		for (int i = 0; i < batchCount; i++)
		{
			if (!data(frameData, batchSize)) return false;
			frameData += batchSize;
		}

		return refresh();
	}

	bool sleep()
	{
		if (!command(0x07)) return false;
		if (!command(0xA5)) return false;
		return true;
	}

	enum class Color : char
	{
		BLACK = 0x0,
		WHITE = 0x1,
		YELLOW = 0x2,
		RED = 0x3,
		BLUE = 0x5,
		GREEN = 0x6,
	};

	constexpr static Color ColorMap[] =
	{
		Color::BLACK,
		Color::WHITE,
		Color::YELLOW,
		Color::RED,
		Color::BLUE,
		Color::GREEN,
	};

	class Frame
	{
	public:
		constexpr auto operator[](short x, short y) { return PixelProxy{ *this, x, y }; }
		constexpr auto operator[](short y) { return LineProxy{ *this, y }; }

	protected:
		uint8_t data[Height][Width / 2]{}; // 单像素uint4
		friend class JD7601;
		friend class PixelProxy;
	};

	class LineProxy
	{
	public:
		Frame& frame;
		short y{};
		constexpr auto operator[](short x) { return PixelProxy{ frame, x, y }; }
	};

	class PixelProxy
	{
	public:
		Frame& frame;
		short x{};
		short y{};

		constexpr operator Color()
		{
			return (Color)((frame.data[y][x / 2] >> (4 ^ ((x & 0x01) * 4))) & 0x0F);
		}

		constexpr auto& operator= (Color color)
		{
			if (x & 0x01) frame.data[y][x / 2] = (frame.data[y][x / 2] & 0xF0) | (uint8_t)color;
			else frame.data[y][x / 2] = (frame.data[y][x / 2] & 0x0F) | ((uint8_t)color << 4);
			return *this;
		}
	};

	Frame* frame{};

	auto operator[](short x, short y) { return (*frame)[x, y]; }
	auto operator[](short y) { return (*frame)[y]; }

private:
	constexpr static char TAG[] = "JD7601";

	SPIDevice spi{};
	GPIO resetGpio{};
	GPIO dcGpio{};
	GPIO idleGpio{};

	using SmallData_t = SPIDevice::SmallData_t;

	static void setDcCommand(void* param)
	{
		auto& self = *(JD7601*)param;
		self.dcGpio = false;
	}

	static void setDcData(void* param)
	{
		auto& self = *(JD7601*)param;
		self.dcGpio = true;
	}

	bool command(uint8_t command, SPIDevice::function_t callbackAfter = SPIDevice::emptyCallback, void* param = nullptr)
	{
		return spi.transmit(SmallData_t{ command,0,0,0 }, 8, setDcCommand, this, callbackAfter, param);
	}

	bool data(uint8_t data, SPIDevice::function_t callbackAfter = SPIDevice::emptyCallback, void* param = nullptr)
	{
		return spi.transmit(SmallData_t{ data,0,0,0 }, 8, setDcData, this, callbackAfter, param);
	}

	bool data(uint8_t* data, size_t size, SPIDevice::function_t callbackAfter = SPIDevice::emptyCallback, void* param = nullptr)
	{
		return spi.transmit(data, size * 8, setDcData, this, callbackAfter, param);
	}

	bool refresh()
	{
		if (!command(0x04)) return false; // PON

		if (!command(0x12)) return false; // DRF
		if (!data(0x00)) return false;

		if (!command(0x02)) return false; // POF
		if (!data(0x00)) return false;
		return true;
	}
};
