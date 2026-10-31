#pragma once

#include "driver/lcd/drawable.hpp"
#include <algorithm>

template<class DrawTarget>
class Rectangle final : public Drawable<DrawTarget>
{
public:
	Vector2s position{};
	Vector2s size{};
	DrawTarget::Color color{};

	virtual void drawTo(DrawTarget& target, Vector2s offset = {}) override final
	{
		Vector2s drawStart = position + offset;
		Vector2s drawEnd = drawStart + size;

		drawStart.x = std::clamp(drawStart.x, (short)0, DrawTarget::Size.x);
		drawStart.y = std::clamp(drawStart.y, (short)0, DrawTarget::Size.y);
		drawEnd.x = std::clamp(drawEnd.x, (short)0, DrawTarget::Size.x);
		drawEnd.y = std::clamp(drawEnd.y, (short)0, DrawTarget::Size.y);

		for (unsigned short y = drawStart.y; y < drawEnd.y; y++)
			for (unsigned short x = drawStart.x; x < drawEnd.x; x++)
				target[x, y] = color;
	}
};

template<class DrawTarget>
class RectangleExtend final : public Drawable<DrawTarget>
{
public:
	Vector2s position{};
	Vector2s size{};
	DrawTarget::Color color[2][2]{};

	virtual void drawTo(DrawTarget& target, Vector2s offset = {}) override final
	{
		Vector2s drawStart = position + offset;
		Vector2s drawEnd = drawStart + size;

		drawStart.x = std::clamp(drawStart.x, (short)0, DrawTarget::Size.x);
		drawStart.y = std::clamp(drawStart.y, (short)0, DrawTarget::Size.y);
		drawEnd.x = std::clamp(drawEnd.x, (short)0, DrawTarget::Size.x);
		drawEnd.y = std::clamp(drawEnd.y, (short)0, DrawTarget::Size.y);

		for (unsigned short y = drawStart.y; y < drawEnd.y; y++)
			for (unsigned short x = drawStart.x; x < drawEnd.x; x++)
				target[x, y] = color[y & 0x01][x & 0x01];
	}
};
