#include "driver/lcd/JD7601.hpp"
#include "driver/task/task.hpp"

#include <esp_event.h>
#include "driver/wifi/nvs.hpp"
#include "driver/wifi/wifi.hpp"
#include "driver/wifi/http.hpp"
#include "server.hpp"

#include "driver/touchSensor/touchSensor.hpp"

using EPD = JD7601;
using Color = EPD::Color;
constexpr auto& ColorMap = EPD::ColorMap;

SPI spiHost{};
EPD screen{};
DMA_ATTR EPD::Frame frame{};
GPIO led{ GPIO_NUM_17 };

TouchSensorHost* touchSensorHost{};
TouchSensor* touchSensor{};

Thread screenThread{};
Thread wifiThread{};

static void updateLed(void* = nullptr)
{
	bool ledStatus = !(bool)GPIO { GPIO_NUM_0 } || (!screen.isSleeping() && screen.isBusy());
	led = !ledStatus;
}

void wifiMain(void*)
{
	constexpr static char TAG[] = "wifiMain";

	TickType_t ledSparkTime[2] = { pdMS_TO_TICKS(1000), pdMS_TO_TICKS(1000) };
	Thread wifiConnectLedSparkThread = Thread{ [](void* param){ TickType_t* ledSparkTime = (TickType_t*)param; while (true) { vTaskDelay(ledSparkTime[(bool)led]); led = !(bool)led; }  }, "led quick spark" , &ledSparkTime, Thread::Priority::Low };

	if (wifiIsStarted())
	{
		wifiStop();
		ledSparkTime[0] = pdMS_TO_TICKS(2000);
		vTaskDelay(ledSparkTime[0]);
		goto endup;
	}

	if (!wifiIsStarted()) wifiStart();
	if (!wifiStationIsStarted()) wifiStationStart();
	if (!wifiIsConnect())
	{
		if constexpr (const auto firstTimeToWifi = false)
			wifiConnect("ssid", "password");
		else
		{
			auto wifi = wifiStationGetInfo();
			wifiConnect((char*)wifi.ssid, (char*)wifi.password);
		}
	}
	while (wifiIsConnect() != wifiIsWantConnect())
		vTaskDelay(1);

endup:
	wifiConnectLedSparkThread = {}; // 手动进行析构
	updateLed();
	wifiThread = {}; // 这里会直接删除空间，不会自动析构
}

bool updateScreen()
{
	constexpr static char TAG[] = "updateScreen";

	led = false;
	TickType_t ledSparkTime[2] = { pdMS_TO_TICKS(1000), pdMS_TO_TICKS(1000) };
	Thread wifiConnectLedSparkThread = Thread{ [](void* param){ TickType_t* ledSparkTime = (TickType_t*)param; while (true) { vTaskDelay(ledSparkTime[(bool)led]); led = !(bool)led; }  }, "led quick spark" , &ledSparkTime, Thread::Priority::Low };

	if (!wifiIsConnect()) return false;

	ledSparkTime[0] = pdMS_TO_TICKS(125);
	ledSparkTime[1] = pdMS_TO_TICKS(125);

	auto sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) { ESP_LOGE(TAG, "socket failed %d", sock); return false; }

	sockaddr_in address{};
	address.sin_family = AF_INET;
	address.sin_port = htons(3000);
	if (auto err = inet_pton(AF_INET, ServerIp, &address.sin_addr) <= 0) { ESP_LOGE(TAG, "inet_pton error %d", err); return false; }

	if (auto err = connect(sock, (struct sockaddr*)&address, sizeof(address)) < 0){ ESP_LOGE(TAG, "connect error %d", err); return false; }

	IOSocketStream socketStream{};
	socketStream.setSocket(sock);

	HttpRequest request{};
	request.setPath("/imageBinary");
	request.setMethod(HttpMethod::Get);
	request.heads.add({ "Host",Host });
	request.send(socketStream);
	socketStream.sendNow();

	vTaskDelay(pdMS_TO_TICKS(500));

	HttpRespond respond{};
	respond.receive(socketStream);
	ESP_LOGI(TAG, "body length = %d", respond.getBodyLenght());

	auto& frameBuffer = frame.getBuffer();
	char* buffer = (char*)&frameBuffer[0][0];
	const size_t capcity = sizeof(frameBuffer);
	size_t offset{};
	size_t bodyLength = respond.getBodyLenght();
	TickType_t timeout = xTaskGetTickCount() + pdMS_TO_TICKS(1000);
	do
	{
		auto size = socketStream.read(buffer + offset, capcity - offset);
		if (size != 0) timeout = xTaskGetTickCount() + pdMS_TO_TICKS(1000);
		else if (timeout < xTaskGetTickCount())
		{
			ESP_LOGE(TAG, "time out, recieved %d, remian %d", offset, bodyLength);
			break;
		}
		bodyLength -= size;
		offset += size;
	} while (bodyLength > 0);

	socketStream.close();
	return true;
}

void screenMain(void*)
{
	constexpr static char TAG[] = "screenMain";

	if (!updateScreen())
	{
		ESP_LOGE(TAG, "update screen failed");
		goto endup;
	}
	updateLed();

	ESP_LOGI(TAG, "reset screen");
	screen.reset();

	ESP_LOGI(TAG, "init screen");
	if (!screen.init())
	{
		ESP_LOGE(TAG, "screen init failed");
		return;
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

endup:
	screenThread = {};
}

extern "C" void app_main()
{
	constexpr static char TAG[] = "main";

	GPIO{ led, GPIO::Mode::GPIO_MODE_INPUT_OUTPUT_OD, GPIO::Pull::GPIO_FLOATING } = false;
	GPIO::enableGlobalInterrupt();

	esp_event_loop_create_default();
	nvsInit();
	wifiInit();

	touchSensorHost = new TouchSensorHost{};
	touchSensorHost->enable();

	touchSensor = new TouchSensor{ *touchSensorHost, 8 };
	touchSensor->setCallback([](void*)
		{
			if (!wifiIsConnect()) return;
			if (!screenThread.isRunning())
				screenThread = Thread{ screenMain,"screen" };
			updateLed();
		}, [](void*){});

	touchSensorHost->start();

	ESP_LOGI(TAG, "creating spi");
	spiHost = SPI{ SPI2_HOST, GPIO::NC, GPIO_NUM_5, GPIO_NUM_4 };
	ESP_LOGI(TAG, "creating screen");
	screen = EPD{ spiHost, GPIO_NUM_6, GPIO_NUM_7, GPIO_NUM_16, GPIO_NUM_15 };
	screen.frame = &frame;
	screen.idleFunction = updateLed;
	screen.busyFunction = [](void*){led = false;};

	GPIO{ GPIO::GPIO_NUM::GPIO_NUM_0, GPIO::Mode::GPIO_MODE_INPUT, GPIO::Pull::GPIO_PULLUP_ONLY, GPIO::Interrupt::GPIO_INTR_ANYEDGE,
		[](void*)
		{
			if (!wifiThread.isRunning())
				wifiThread = Thread{ wifiMain,"wifi" };
			updateLed();
		} };

	updateLed();

	return;
}
