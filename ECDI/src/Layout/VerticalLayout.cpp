#include "ECDI/Layout/VerticalLayout.h"

#include "ECDI/Widget/Widget.h"
namespace ECDI
{


void VerticalLayout::Arrange(Widget& parent){

	int currentY = 0;


	size_t count = parent.GetChildCount();


	for (size_t i = 0; i < count; i++){

		Widget* child = parent.GetChildAt(i);

		child->SetPosition(0,currentY);

		currentY += child->GetHeight();

	}

}
}
