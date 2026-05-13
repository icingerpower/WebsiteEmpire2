#include "AbstractSecondaryPageBloc.h"

QList<AbstractSocialMedia::ImageSize> AbstractSecondaryPageBloc::requiredImageSizes() const
{
    return {
        AbstractSocialMedia::ImageSize::Landscape,
        AbstractSocialMedia::ImageSize::Wide,
        AbstractSocialMedia::ImageSize::Square,
        AbstractSocialMedia::ImageSize::Portrait,
    };
}
