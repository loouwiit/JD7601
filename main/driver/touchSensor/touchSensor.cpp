#include "touchSensor.hpp"

#include "esp_log.h"

TouchSensorHost::TouchSensorHost()
{
	// 采样配置
	touch_sensor_sample_config_t sample_cfg = TOUCH_SENSOR_V2_DEFAULT_SAMPLE_CONFIG(500, TOUCH_VOLT_LIM_L_0V5, TOUCH_VOLT_LIM_H_1V5);
	// 默认控制器配置
	touch_sensor_config_t touch_cfg = TOUCH_SENSOR_DEFAULT_BASIC_CONFIG(1, &sample_cfg);
	// 申请一个新的触摸传感器控制器句柄
	ESP_ERROR_CHECK(touch_sensor_new_controller(&touch_cfg, &handle));

	// 滤波器
	touch_sensor_filter_config_t filter_config = TOUCH_SENSOR_DEFAULT_FILTER_CONFIG();
	// 注册滤波器
	ESP_ERROR_CHECK(touch_sensor_config_filter(handle, &filter_config));

	// 回调函数
	touch_event_callbacks_t callbacks = {
		.on_active = activeCallback,
		.on_inactive = inactiveCallback,
	};
	ESP_ERROR_CHECK(touch_sensor_register_callbacks(handle, &callbacks, this));
}

TouchSensorHost::~TouchSensorHost()
{
	if (started)
		stop();

	if (enabled)
		disable();

	// 注销滤波器
	if (handle)
	{
		ESP_ERROR_CHECK(touch_sensor_config_filter(handle, NULL));
		ESP_ERROR_CHECK(touch_sensor_del_controller(handle));
		handle = nullptr;
	}
}

void TouchSensorHost::enable()
{
	if (!handle)
	{
		ESP_LOGI(TAG, "Touch sensor not initialized");
		return;
	}

	if (enabled)
	{
		ESP_LOGW(TAG, "Touch sensor already enabled");
	}
	touch_sensor_enable(handle);
	enabled = true;
}

void TouchSensorHost::disable()
{
	if (!handle)
	{
		ESP_LOGE(TAG, "Touch sensor not initialized");
		return;
	}

	if (!enabled)
	{
		ESP_LOGW(TAG, "Touch sensor already disabled");
		return;
	}

	if (started)
		stop();

	touch_sensor_disable(handle);
	enabled = false;
}

void TouchSensorHost::start()
{
	if (!handle)
	{
		ESP_LOGE(TAG, "Touch sensor not initialized");
		return;
	}
	if (!enabled)
	{
		ESP_LOGE(TAG, "Touch sensor not enabled");
		return;
	}
	if (started)
	{
		ESP_LOGW(TAG, "Touch sensor already started");
		return;
	}
	ESP_ERROR_CHECK(touch_sensor_start_continuous_scanning(handle));
}

void TouchSensorHost::stop()
{
	if (!handle)
	{
		ESP_LOGE(TAG, "Touch sensor not initialized");
		return;
	}
	if (!enabled)
	{
		ESP_LOGE(TAG, "Touch sensor not enabled");
		return;
	}
	if (!started)
	{
		ESP_LOGW(TAG, "Touch sensor already stopped");
		return;
	}
	ESP_ERROR_CHECK(touch_sensor_stop_continuous_scanning(handle));
}

void TouchSensorHost::scanOnce()
{
	ESP_ERROR_CHECK(touch_sensor_trigger_oneshot_scanning(handle, 1000));
}

bool TouchSensorHost::activeCallback(touch_sensor_handle_t sens_handle, const touch_prox_done_event_data_t* event, void* user_ctx)
{
	auto& self = *(TouchSensorHost*)user_ctx;
	self.activeCallbacks[event->chan_id](self.params[event->chan_id]);
	return false;
}

bool TouchSensorHost::inactiveCallback(touch_sensor_handle_t sens_handle, const touch_prox_done_event_data_t* event, void* user_ctx)
{
	auto& self = *(TouchSensorHost*)user_ctx;
	self.inctiveCallbacks[event->chan_id](self.params[event->chan_id]);
	return false;
}

TouchSensor::TouchSensor(TouchSensorHost& host, int channelId)
	: host{ &host }, channelId{ channelId }
{
	touch_channel_config_t chan_cfg{};
	chan_cfg.active_thresh[0] = 500;
	chan_cfg.charge_speed = touch_charge_speed_t::TOUCH_CHARGE_SPEED_1;
	chan_cfg.init_charge_volt = touch_init_charge_volt_t::TOUCH_INIT_CHARGE_VOLT_DEFAULT;

	auto hostStarted = host.started;
	auto hostEnabled = host.enabled;
	if (host.enabled)
		host.disable();

	// 申请一个新的触摸通道句柄
	ESP_ERROR_CHECK(touch_sensor_new_channel(host.handle, channelId, &chan_cfg, &chan_handle));

	host.enable();
	for (auto i = 0; i < 10; i++)
		host.scanOnce();

	uint32_t benchmark{};
	touch_channel_read_data(chan_handle, TOUCH_CHAN_DATA_TYPE_BENCHMARK, &benchmark);

	host.disable();
	chan_cfg.active_thresh[0] = (uint32_t)(benchmark * 0.03f);
	touch_sensor_reconfig_channel(chan_handle, &chan_cfg);
	ESP_LOGI(TAG, "thresh = %d", chan_cfg.active_thresh[0]);

	if (hostEnabled)
		host.enable();
	if (hostStarted)
		host.start();
}

TouchSensor::~TouchSensor()
{
	ESP_ERROR_CHECK(touch_sensor_del_channel(chan_handle));
}

void TouchSensor::setCallback(Callback_t active, Callback_t inactive, void* param)
{
	if (host == nullptr)
	{
		ESP_LOGE(TAG, "host is null");
		return;
	}

	if (channelId == -1)
	{
		ESP_LOGE(TAG, "channelId is invalid");
		return;
	}

	host->activeCallbacks[channelId] = active;
	host->inctiveCallbacks[channelId] = inactive;
	host->params[channelId] = param;
}

uint32_t TouchSensor::read()
{
	uint32_t smooth_data[1] = {};
	touch_channel_read_data(chan_handle, TOUCH_CHAN_DATA_TYPE_SMOOTH, smooth_data);
	return smooth_data[0];
}
