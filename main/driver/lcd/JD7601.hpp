#pragma once

#include "utility"
#include "esp_log.h"
#include "driver/spi/spi.hpp"

#include "driver/vector/vector.hpp"
#include "driver/lcd/rectangle.hpp"

class JD7601
{
public:
	constexpr static Vector2s Size = { 720, 480 };

	enum class FlushStatus
	{
		None,
		Pon,
		Drf,
		Pof,
	};

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

	using Rectangle = ::Rectangle<JD7601>;
	using RectangleExtend = ::RectangleExtend<JD7601>;

	class Frame
	{
	public:
		constexpr auto operator[](Vector2s position) { return PixelProxy{ *this, position }; }
		constexpr auto operator[](short x, short y) { return PixelProxy{ *this,{ x, y } }; }
		constexpr auto operator[](short y) { return LineProxy{ *this, y }; }

	protected:
		uint8_t data[Size.y][Size.x / 2]{}; // 单像素uint4
		friend class JD7601;
		friend class PixelProxy;
	};

	class LineProxy
	{
	public:
		Frame& frame;
		short y{};
		constexpr auto operator[](short x) { return PixelProxy{ frame,{ x, y } }; }
	};

	class PixelProxy
	{
	public:
		Frame& frame;
		Vector2s position{};

		constexpr operator Color()
		{
			return (Color)((frame.data[position.y][position.x / 2] >> (4 ^ ((position.x & 0x01) * 4))) & 0x0F);
		}

		constexpr auto& operator= (Color color)
		{
			if (position.x & 0x01) frame.data[position.y][position.x / 2] = (frame.data[position.y][position.x / 2] & 0xF0) | (uint8_t)color;
			else frame.data[position.y][position.x / 2] = (frame.data[position.y][position.x / 2] & 0x0F) | ((uint8_t)color << 4);
			return *this;
		}
	};

	Frame* frame{};
	using function_t = void (*)(void*);
	function_t idleFunction{};
	function_t busyFunction{};
	void* functionParam{};

	JD7601() = default;
	JD7601(SPI& spiHost, GPIO reset, GPIO dc, GPIO idle, GPIO cs = GPIO::NC, unsigned char transmitionSize = 16, int speed = 2000000) : spi{ spiHost, cs, transmitionSize, speed }, resetGpio{ reset, GPIO::Mode::GPIO_MODE_OUTPUT }, dcGpio{ dc, GPIO::Mode::GPIO_MODE_OUTPUT }, idleGpio{ idle, GPIO::Mode::GPIO_MODE_INPUT , GPIO::Pull::GPIO_FLOATING, GPIO::Interrupt::GPIO_INTR_POSEDGE, idleCallback, this } {}
	JD7601(JD7601&& move) { operator=(std::move(move)); }
	JD7601& operator=(JD7601&& move)
	{
		std::swap(move.frame, frame);
		std::swap(move.resetGpio, resetGpio);
		std::swap(move.dcGpio, dcGpio);
		std::swap(move.idleGpio, idleGpio);
		std::swap(move.spi, spi);
		std::swap(move.flushStatus, flushStatus);
		std::swap(move.sleeping, sleeping);

		idleGpio.setInterrupt(GPIO::Interrupt::GPIO_INTR_DISABLE);
		idleGpio.setInterrupt(GPIO::Interrupt::GPIO_INTR_POSEDGE, idleCallback, this);

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
		sleeping = false;
		return true;
	}

	bool init()
	{
		busyFunction(functionParam);
		if (!wait(pdMS_TO_TICKS(5000))) return false;
		if (!command(0xE9)) return false;
		if (!data(0x01)) return false;
		return true;
	}

	bool isSleeping()
	{
		return sleeping;
	}

	bool isBusy(FlushStatus targetStatus = FlushStatus::None)
	{
		return flushStatus != targetStatus || spi.getTransmittingCount() != 0 || !(bool)idleGpio;
	}

	bool wait(TickType_t maxWaitTime = pdMS_TO_TICKS(60 * 1000), FlushStatus targetStatus = FlushStatus::None)
	{
		TickType_t timeOut = xTaskGetTickCount() + maxWaitTime;
		while (isBusy(targetStatus) && xTaskGetTickCount() < timeOut)
			vTaskDelay(1);
		if (flushStatus != targetStatus)
			ESP_LOGE(TAG, "still flushing");
		if (spi.getTransmittingCount() != 0)
			ESP_LOGE(TAG, "still transmiting");
		if ((bool)idleGpio == false)
			ESP_LOGE(TAG, "still busy");
		return !isBusy(targetStatus);
	}

	bool flush()
	{
		busyFunction(functionParam);
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
		if (!wait(pdMS_TO_TICKS(60 * 1000))) return false;
		if (!command(0x07)) return false;
		if (!command(0xA5)) return false;
		sleeping = true;
		return true;
	}

	auto operator[](short x, short y) { return (*frame)[x, y]; }
	auto operator[](short y) { return (*frame)[y]; }

	decltype(auto) draw(auto&& element)
	{
		return element.drawTo(*this);
	}

private:
	constexpr static char TAG[] = "JD7601";

	SPIDevice spi{};
	GPIO resetGpio{};
	GPIO dcGpio{};
	GPIO idleGpio{};
	bool sleeping{ true };

	using SmallData_t = SPIDevice::SmallData_t;

	FlushStatus flushStatus = FlushStatus::None;

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

	static void idleCallback(void* param)
	{
		auto& self = *(JD7601*)param;
		switch (self.flushStatus)
		{
		case FlushStatus::Drf:
			self.flushStatus = FlushStatus::Pof;
			self.sendPof();
			break;
		case FlushStatus::Pof:
			self.flushStatus = FlushStatus::None;
			if (self.idleFunction)
				self.idleFunction(self.functionParam);
		default:
			break;
		}
	}

	bool sendPonDrf()
	{
		if (flushStatus != FlushStatus::Pon) return false;
		if (!command(0x04)) return false; // PON
		if (!wait(pdMS_TO_TICKS(1000), FlushStatus::Pon)) return false;
		flushStatus = FlushStatus::Drf;
		if (!command(0x12)) return false; // DRF
		if (!data(0x00)) return false;
		return true;
	}

	bool sendPof()
	{
		if (flushStatus != FlushStatus::Pof) return false;
		if (!command(0x02)) return false; // POF
		if (!data(0x00)) return false;
		return true;
	}

	bool refresh()
	{
		if (flushStatus != FlushStatus::None) return false;
		flushStatus = FlushStatus::Pon;
		return sendPonDrf();
	}
};
