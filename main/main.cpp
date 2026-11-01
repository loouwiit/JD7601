#include "driver/lcd/JD7601.hpp"

using EPD = JD7601;
using Color = EPD::Color;
constexpr auto& ColorMap = EPD::ColorMap;

SPI spiHost{};
EPD screen{};
DMA_ATTR EPD::Frame frame{};

extern "C" void app_main(void)
{
	constexpr static char TAG[] = "main";

	GPIO::enableGlobalInterrupt();

	vTaskDelay(1000);

	ESP_LOGI(TAG, "creating spi");
	spiHost = SPI{ SPI2_HOST, GPIO::NC, GPIO_NUM_5, GPIO_NUM_4 };
	ESP_LOGI(TAG, "creating screen");
	screen = EPD{ spiHost, GPIO_NUM_6, GPIO_NUM_7, GPIO_NUM_16, GPIO_NUM_15 };
	screen.frame = &frame;

	ESP_LOGI(TAG, "reset screen");
	screen.reset();

	ESP_LOGI(TAG, "init screen");
	if (!screen.init())
	{
		ESP_LOGE(TAG, "screen init failed");
		return;
	}

	EPD::RectangleExtend rectangle{};
	rectangle.size = { 40,60 };

	int nowColor[4] = {};

	auto processColor = [&]()
		{
			if (++nowColor[0] >= 6)
				nowColor[0] = nowColor[1]++ + 1;
			if (nowColor[1] >= 6)
				nowColor[1] = nowColor[2]++ + 1;
			if (nowColor[2] >= 6)
				nowColor[2] = nowColor[3]++ + 1;
			if (nowColor[3] >= 6)
				nowColor[3] = 0;
			if (nowColor[2] >= 6)
				nowColor[2] = nowColor[3];
			if (nowColor[1] >= 6)
				nowColor[1] = nowColor[2];
			if (nowColor[0] >= 6)
				nowColor[0] = nowColor[1];
		};

	for (rectangle.position.y = 0; rectangle.position.y < EPD::Size.y; rectangle.position.y += rectangle.size.y)
		for (rectangle.position.x = 0; rectangle.position.x < EPD::Size.x; rectangle.position.x += rectangle.size.x)
		{
			rectangle.color[0][0] = ColorMap[nowColor[0]];
			rectangle.color[0][1] = ColorMap[nowColor[1]];
			rectangle.color[1][0] = ColorMap[nowColor[2]];
			rectangle.color[1][1] = ColorMap[nowColor[3]];
			screen.draw(rectangle);
			processColor();
		}

	ESP_LOGI(TAG, "flush screen");
	if (!screen.flush()) ESP_LOGE(TAG, "screen flush failed");
	if (!screen.wait())
	{
		ESP_LOGE(TAG, "screen wait failed");
		return;
	}

	ESP_LOGI(TAG, "sleep screen");
	screen.sleep();

	return;
}
