//
// Kernel Device
//
#include "DeviceContext.hpp"
#include "../kdebug.hpp"

using namespace Chino;
using namespace Chino::Graphics;

size_t Chino::Graphics::GetPixelBytes(ColorFormat format)
{
	switch (format)
	{
	case ColorFormat::B5G6R5_UNORM:
		return 2;
	case ColorFormat::R32G32B32A32_FLOAT:
		return 16;
	default:
		throw std::invalid_argument("Invalid format.");
	}
}

static void CopyBits(const uint8_t* src, size_t srcStride, uint8_t* dest, size_t destStride, size_t lineSize, size_t height)
{
	for (size_t y = 0; y < height; y++)
	{
		auto begin = src + y * srcStride;
		std::copy(begin, begin + lineSize, dest + y * destStride);
	}
}

static void FillBits(const SurfaceData& data, ColorFormat format, const ColorValue& color)
{
	if (format == ColorFormat::B5G6R5_UNORM)
	{
		gsl::span<uint16_t> span = { reinterpret_cast<uint16_t*>(data.Data.data()), data.Data.size() / 2 };

		auto src = span.data();
		auto value = Rgb565::From(color).Value;
		for (size_t y = 0; y < data.Rect.GetSize().Height; y++)
		{
			for (size_t x = 0; x < data.Rect.GetSize().Width; x++)
				src[x] = value;

			src += data.Stride / 2;
		}
	}
	else
	{
		throw std::runtime_error("Not implemented.");
	}
}

class SoftwareSurface : public Surface
{
public:
	SoftwareSurface(ColorFormat format, const SizeU& size)
		:format_(format), size_(size), stride_(size.Width * GetPixelBytes(format))
	{
		auto bytes = stride_ * size.Height;
		storage_ = std::make_unique<uint8_t[]>(bytes);
		data_ = { storage_.get(), ptrdiff_t(bytes) };
	}

	SoftwareSurface(ColorFormat format, const SizeU& size, const SurfaceData& data, bool copy)
		:format_(format), size_(size)
	{
		if (!copy)
		{
			stride_ = data.Stride;
			data_ = data.Data;

			auto bytes = stride_ * size.Height;
			kassert(bytes == data_.size_bytes());
		}
		else
		{
			stride_ = size.Width * GetPixelBytes(format);
			auto bytes = stride_ * size.Height;
			storage_ = std::make_unique<uint8_t[]>(bytes);
			data_ = { storage_.get(), ptrdiff_t(bytes) };
			CopyBits(data.Data.data(), data.Stride, data_.data(), stride_, stride_, size.Height);
		}
	}

	virtual SizeU GetPixelSize() noexcept
	{
		return size_;
	}

	virtual ColorFormat GetFormat() noexcept
	{
		return format_;
	}

	virtual SurfaceLocation GetLocation() noexcept override
	{
		return SurfaceLocation::SystemMemory;
	}

	virtual SurfaceData Lock(const RectU& rect) override
	{
		auto begin = rect.Top * stride_ + GetPixelBytes(format_) * rect.Left;
		auto end = (int32_t(rect.Bottom - 1)) * stride_ + GetPixelBytes(format_) * rect.Right;

		if (begin > data_.size_bytes() || end > data_.size_bytes())
			throw std::out_of_range("Lock rect is out of range.");

		return { { data_.data() + begin, data_.data() + end }, stride_, rect };
	}

	virtual void Unlock(SurfaceData& data) override
	{

	}
private:
	ColorFormat format_; 
	SizeU size_;
	size_t stride_;

	gsl::span<uint8_t> data_;
	std::unique_ptr<uint8_t[]> storage_;
};

DeviceContext::DeviceContext(ObjectAccessor<Device::DisplayDevice>&& device)
	:device_(std::move(device))
{
	primarySurface_ = device_->OpenPrimarySurface();
}

ObjectPtr<Surface> DeviceContext::CreatePrimarySurface() noexcept
{
	return primarySurface_;
}

ObjectPtr<Surface> DeviceContext::CreateOffscreenSurface(ColorFormat format, const SizeU& size)
{
	return MakeObject<SoftwareSurface>(format, size);
}

ObjectPtr<Surface> DeviceContext::CreateOffscreenSurface(ColorFormat format, const SizeU& size, const SurfaceData& data, bool copy)
{
	return MakeObject<SoftwareSurface>(format, size, data, copy);
}

void DeviceContext::Clear(Surface& src, const RectU& srcRect, const ColorValue& color)
{
	if (src.GetLocation() == SurfaceLocation::DeviceMemory)
	{
		device_->Clear(src, srcRect, color);
	}
	else
	{
		auto srcLocker = src.Lock(srcRect);
		FillBits(srcLocker, src.GetFormat(), color);
		src.Unlock(srcLocker);
	}
}

void DeviceContext::CopySubresource(Surface& src, Surface& dest, const RectU& srcRect, const PointU& destPosition)
{
	if (src.GetFormat() != dest.GetFormat())
		throw std::invalid_argument("Src and dest must have same format.");

	if (src.GetLocation() == SurfaceLocation::SystemMemory &&
		dest.GetLocation() == SurfaceLocation::SystemMemory)
	{
		auto srcLocker = src.Lock(srcRect);
		auto destLocker = dest.Lock({ destPosition, srcRect.GetSize() });
		auto lineSize = srcRect.GetSize().Width * GetPixelBytes(src.GetFormat());
		CopyBits(srcLocker.Data.data(), srcLocker.Stride, destLocker.Data.data(), destLocker.Stride, lineSize, srcRect.GetSize().Height);
		dest.Unlock(destLocker);
		src.Unlock(srcLocker);
	}
	else
	{
		device_->CopySubresource(src, dest, srcRect, destPosition);
	}
}
