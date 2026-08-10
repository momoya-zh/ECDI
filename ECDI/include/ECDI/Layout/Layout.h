#pragma once
namespace ECDI
{

class Widget;

class Layout{

public:

    virtual ~Layout() = default;

    virtual void Arrange(Widget& parent) = 0;

};
}
