#pragma once

#include "ECDI/Layout/Layout.h"

namespace ECDI{


class VerticalLayout : public Layout{

public:

    void Arrange(Widget& parent) override;

};

}
