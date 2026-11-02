#pragma once

#include "driver/touch_sens.h"

class TouchSensorHost
{
public:
	TouchSensorHost();
	~TouchSensorHost();

	void enable();
	void disable();

	void start();
	void stop();
	void scanOnce();

	using Callback_t = void(*)(void*);

private:
	constexpr static char TAG[] = "TouchSensorHost";

	touch_sensor_handle_t handle{};
	bool enabled{};
	bool started{};
	friend class TouchSensor;

	static bool activeCallback(touch_sensor_handle_t sens_handle, const touch_prox_done_event_data_t* event, void* user_ctx);
	static bool inactiveCallback(touch_sensor_handle_t sens_handle, const touch_prox_done_event_data_t* event, void* user_ctx);

	Callback_t activeCallbacks[SOC_TOUCH_SENSOR_NUM]{};
	Callback_t inctiveCallbacks[SOC_TOUCH_SENSOR_NUM]{};
	void* params[SOC_TOUCH_SENSOR_NUM]{};
};

class TouchSensor
{
public:
	TouchSensor(TouchSensorHost& host, int channelId);
	~TouchSensor();

	using Callback_t = TouchSensorHost::Callback_t;

	void setCallback(Callback_t active, Callback_t inactive, void* param = nullptr);

	uint32_t read();
private:
	constexpr static char TAG[] = "TouchSensor";

	TouchSensorHost* host{};
	int channelId{ -1 };
	touch_channel_handle_t chan_handle{};
};
