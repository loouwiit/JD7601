#pragma once

#include "driver/vector/vector.hpp"

template<class DrawTarget>
class Drawable
{
public:
	virtual void drawTo(DrawTarget&, Vector2s offset = {}) = 0;
	~Drawable() {};
};
