#include "ECDI/Widget/Panel.h"

#include<Windows.h>
namespace ECDI
{

void Panel::OnPaint(HDC hdc,int x,int y)
{
    RECT rect{
        x,

        y,

        x + GetWidth(),

        y + GetHeight()

    };

    FillRect(hdc,&rect,(HBRUSH)GetStockObject(LTGRAY_BRUSH));

}
}
