#include "driver/lcd/JD7601.hpp"

SPI spiHost{};
JD7601 screen{};
DMA_ATTR JD7601::Frame frame{};
using Color = JD7601::Color;

extern "C" void app_main(void)
{
	constexpr static char TAG[] = "main";

	vTaskDelay(1000);

	ESP_LOGI(TAG, "creating spi");
	spiHost = SPI{ SPI2_HOST, GPIO::NC, GPIO_NUM_5, GPIO_NUM_4 };
	ESP_LOGI(TAG, "creating screen");
	screen = JD7601{ spiHost, GPIO_NUM_6, GPIO_NUM_7, GPIO_NUM_16, GPIO_NUM_15 };
	screen.frame = &frame;

	ESP_LOGI(TAG, "reset screen");
	screen.reset();

	ESP_LOGI(TAG, "init screen");
	if (!screen.init())
	{
		ESP_LOGE(TAG, "screen init failed");
		return;
	}

	for (int i = 0; i < screen.Width; i++)
		screen[60][i] = Color::YELLOW;

	screen[10, 10] = Color::RED;
	screen[10, 11] = Color::RED;
	screen[11, 10] = Color::RED;
	screen[11, 11] = Color::RED;
	for (int i = 0; i < screen.Height; i++)
	{
		screen[i][20] = Color::WHITE;
		screen[i][21] = Color::YELLOW;
		screen[i][22] = Color::RED;
		screen[i][23] = Color::BLUE;
		screen[i][24] = Color::GREEN;
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
