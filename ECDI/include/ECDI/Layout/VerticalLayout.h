#pragma once

#include "Layout.h"

namespace ECDI{


class VerticalLayout : public Layout{

public:

    void Arrange(Widget& parent) override;

};

}