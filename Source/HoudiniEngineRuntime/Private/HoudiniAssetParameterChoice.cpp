/*
* Copyright (c) <2017> Side Effects Software Inc.
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*
* Produced by:
*      Mykola Konyk
*      Side Effects Software Inc
*      123 Front Street West, Suite 1401
*      Toronto, Ontario
*      Canada   M5J 2M2
*      416-504-9876
*
*/

#include "HoudiniEngineRuntimePrivatePCH.h"
#include "HoudiniAssetParameterChoice.h"
#include "HoudiniAssetComponent.h"
#include "HoudiniEngine.h"
#include "HoudiniEngineUtils.h"
#include "HoudiniApi.h"
#include "HoudiniEngineString.h"

UHoudiniAssetParameterChoice::UHoudiniAssetParameterChoice( const FObjectInitializer & ObjectInitializer )
    : Super( ObjectInitializer )
    , CurrentValue( 0 )
    , bStringChoiceList( false )
{
    StringValue = TEXT( "" );
}

UHoudiniAssetParameterChoice::~UHoudiniAssetParameterChoice()
{}

UHoudiniAssetParameterChoice *
UHoudiniAssetParameterChoice::Create(
    UObject * InPrimaryObject,
    UHoudiniAssetParameter * InParentParameter,
    HAPI_NodeId InNodeId,
    const HAPI_ParmInfo & ParmInfo )
{
    UObject * Outer = InPrimaryObject;
    if ( !Outer )
    {
        Outer = InParentParameter;
        if ( !Outer )
        {
            // Must have either component or parent not null.
            check( false );
        }
    }

    UHoudiniAssetParameterChoice * HoudiniAssetParameterChoice = NewObject<UHoudiniAssetParameterChoice>(
        Outer, UHoudiniAssetParameterChoice::StaticClass(), NAME_None, RF_Public | RF_Transactional );

    HoudiniAssetParameterChoice->CreateParameter( InPrimaryObject, InParentParameter, InNodeId, ParmInfo );
    return HoudiniAssetParameterChoice;
}

bool
UHoudiniAssetParameterChoice::CreateParameter(
    UObject * InPrimaryObject,
    UHoudiniAssetParameter * InParentParameter,
    HAPI_NodeId InNodeId,
    const HAPI_ParmInfo & ParmInfo )
{
    if ( !Super::CreateParameter( InPrimaryObject, InParentParameter, InNodeId, ParmInfo ) )
        return false;

    // Choice lists can be only integer or string.
    if ( ParmInfo.type != HAPI_PARMTYPE_INT && ParmInfo.type != HAPI_PARMTYPE_STRING )
        return false;

    // Get the actual value for this property.
    CurrentValue = 0;

    if ( ParmInfo.type == HAPI_PARMTYPE_INT )
    {
        // This is an integer choice list.
        bStringChoiceList = false;

        // Assign internal Hapi values index.
        SetValuesIndex( ParmInfo.intValuesIndex );

        if ( FHoudiniApi::GetParmIntValues(
            FHoudiniEngine::Get().GetSession(), NodeId, &CurrentValue,
            ValuesIndex, TupleSize) != HAPI_RESULT_SUCCESS )
        {
            return false;
        }
    }
    else if ( ParmInfo.type == HAPI_PARMTYPE_STRING )
    {
        // This is a string choice list.
        bStringChoiceList = true;

        // Assign internal Hapi values index.
        SetValuesIndex( ParmInfo.stringValuesIndex );

        HAPI_StringHandle StringHandle;
        if ( FHoudiniApi::GetParmStringValues(
            FHoudiniEngine::Get().GetSession(), NodeId, false,
            &StringHandle, ValuesIndex, TupleSize ) != HAPI_RESULT_SUCCESS )
        {
            return false;
        }

        // Get the actual string value.
        FHoudiniEngineString HoudiniEngineString( StringHandle );
        if ( !HoudiniEngineString.ToFString( StringValue ) )
            return false;
    }

    // Get choice descriptors.
    TArray< HAPI_ParmChoiceInfo > ParmChoices;
    ParmChoices.SetNumZeroed( ParmInfo.choiceCount );
    if ( FHoudiniApi::GetParmChoiceLists(
        FHoudiniEngine::Get().GetSession(), NodeId, &ParmChoices[ 0 ],
        ParmInfo.choiceIndex, ParmInfo.choiceCount ) != HAPI_RESULT_SUCCESS )
    {
        return false;
    }

    // Get string values for all available choices.
    StringChoiceValues.Empty();
    StringChoiceLabels.Empty();

    bool bMatchedSelectionLabel = false;
    for ( int32 ChoiceIdx = 0; ChoiceIdx < ParmChoices.Num(); ++ChoiceIdx )
    {
        FString * ChoiceValue = new FString();
        FString * ChoiceLabel = new FString();

        {
            FHoudiniEngineString HoudiniEngineString( ParmChoices[ ChoiceIdx ].valueSH );
            if ( !HoudiniEngineString.ToFString( *ChoiceValue ) )
                return false;

            StringChoiceValues.Add( TSharedPtr< FString >( ChoiceValue ) );
        }

        {
            FHoudiniEngineString HoudiniEngineString( ParmChoices[ ChoiceIdx ].labelSH );
            if ( !HoudiniEngineString.ToFString( *ChoiceLabel ) )
                return false;

            StringChoiceLabels.Add( TSharedPtr< FString >( ChoiceLabel ) );
        }

        // If this is a string choice list, we need to match name with corresponding selection label.
        if ( bStringChoiceList && !bMatchedSelectionLabel && ChoiceValue->Equals( StringValue ) )
        {
            StringValue = *ChoiceLabel;
            bMatchedSelectionLabel = true;
            CurrentValue = ChoiceIdx;
        }
        else if ( !bStringChoiceList && ChoiceIdx == CurrentValue )
        {
            StringValue = *ChoiceLabel;
        }
    }

    return true;
}

#if WITH_EDITOR

void
UHoudiniAssetParameterChoice::CreateWidget( IDetailCategoryBuilder & localDetailCategoryBuilder )
{
    Super::CreateWidget( localDetailCategoryBuilder );

    FDetailWidgetRow & Row = localDetailCategoryBuilder.AddCustomRow( FText::GetEmpty() );

    // Create the standard parameter name widget.
    CreateNameWidget( Row, true );

    TSharedRef< SHorizontalBox > HorizontalBox = SNew( SHorizontalBox );
    TSharedPtr< SComboBox< TSharedPtr< FString > > > ComboBox;

    HorizontalBox->AddSlot().Padding( 2, 2, 5, 2 )
    [
        SAssignNew( ComboBox, SComboBox< TSharedPtr< FString > > )
        .OptionsSource( &StringChoiceLabels )
        .InitiallySelectedItem( StringChoiceLabels[ CurrentValue ] )
        .OnGenerateWidget( SComboBox< TSharedPtr< FString > >::FOnGenerateWidget::CreateUObject(
            this, &UHoudiniAssetParameterChoice::CreateChoiceEntryWidget ) )
        .OnSelectionChanged( SComboBox< TSharedPtr< FString > >::FOnSelectionChanged::CreateUObject(
            this, &UHoudiniAssetParameterChoice::OnChoiceChange ) )
        [
            SNew( STextBlock )
            .Text( TAttribute< FText >::Create( TAttribute< FText >::FGetter::CreateUObject(
                this, &UHoudiniAssetParameterChoice::HandleChoiceContentText ) ) )
            .Font( FEditorStyle::GetFontStyle( TEXT( "PropertyWindow.NormalFont" ) ) )
        ]
    ];

    if ( ComboBox.IsValid() )
        ComboBox->SetEnabled( !bIsDisabled );

    Row.ValueWidget.Widget = HorizontalBox;
    Row.ValueWidget.MinDesiredWidth( HAPI_UNREAL_DESIRED_ROW_VALUE_WIDGET_WIDTH );
}

void
UHoudiniAssetParameterChoice::CreateWidget( TSharedPtr< SVerticalBox > VerticalBox )
{
    Super::CreateWidget( VerticalBox );

    FText ParameterLabelText = FText::FromString( GetParameterLabel() );

    VerticalBox->AddSlot().Padding( 2, 2, 2, 2 )
    [
        SNew( SHorizontalBox )
        +SHorizontalBox::Slot().MaxWidth( 80 ).Padding( 7, 1, 0, 0 ).VAlign( VAlign_Center )
        [
            SNew( STextBlock )
            .Text( ParameterLabelText )
            .ToolTipText( ParameterLabelText )
            .Font( FEditorStyle::GetFontStyle( TEXT( "PropertyWindow.NormalFont" ) ) )
        ]
        +SHorizontalBox::Slot()
        [
            SNew( SComboBox< TSharedPtr< FString > > )
            .OptionsSource( &StringChoiceLabels )
            .InitiallySelectedItem( StringChoiceLabels[ CurrentValue ] )
            .OnGenerateWidget( SComboBox< TSharedPtr< FString > >::FOnGenerateWidget::CreateUObject(
                this, &UHoudiniAssetParameterChoice::CreateChoiceEntryWidget ) )
            .OnSelectionChanged( SComboBox< TSharedPtr< FString > >::FOnSelectionChanged::CreateUObject(
                this, &UHoudiniAssetParameterChoice::OnChoiceChange ) )
            [
                SNew( STextBlock )
                .Text( TAttribute< FText >::Create( TAttribute< FText >::FGetter::CreateUObject(
                    this, &UHoudiniAssetParameterChoice::HandleChoiceContentText ) ) )
                .Font( FEditorStyle::GetFontStyle( TEXT( "PropertyWindow.NormalFont" ) ) )
            ]
        ]
    ];
}

#endif // WITH_EDITOR

TOptional< TSharedPtr< FString > >
UHoudiniAssetParameterChoice::GetValue( int32 Idx ) const
{
    if ( Idx == 0 )
        return TOptional< TSharedPtr< FString > >( StringChoiceValues[ CurrentValue ] );

    return TOptional< TSharedPtr< FString > >();
}

int32
UHoudiniAssetParameterChoice::GetParameterValueInt() const
{
    return CurrentValue;
}

const FString &
UHoudiniAssetParameterChoice::GetParameterValueString() const
{
    return StringValue;
}

bool
UHoudiniAssetParameterChoice::IsStringChoiceList() const
{
    return bStringChoiceList;
}

void
UHoudiniAssetParameterChoice::SetValueInt( int32 Value, bool bTriggerModify, bool bRecordUndo )
{
#if WITH_EDITOR

    FScopedTransaction Transaction(
        TEXT( HOUDINI_MODULE_RUNTIME ),
        LOCTEXT( "HoudiniAssetParameterChoiceChange", "Houdini Parameter Choice: Changing a value" ),
        PrimaryObject );
    Modify();

    if ( !bRecordUndo )
        Transaction.Cancel();

#endif // WITH_EDITOR

    MarkPreChanged( bTriggerModify );

    CurrentValue = Value;

    MarkChanged( bTriggerModify );
}

bool
UHoudiniAssetParameterChoice::UploadParameterValue()
{
    if ( bStringChoiceList )
    {
        // Get corresponding value.
        FString* ChoiceValue = StringChoiceValues[ CurrentValue ].Get();
        std::string String = TCHAR_TO_UTF8( *( *ChoiceValue ) );
        FHoudiniApi::SetParmStringValue( FHoudiniEngine::Get().GetSession(), NodeId, String.c_str(), ParmId, 0 );
    }
    else
    {
        // This is an int choice list.
        FHoudiniApi::SetParmIntValues( FHoudiniEngine::Get().GetSession(), NodeId, &CurrentValue, ValuesIndex, TupleSize );
    }

    return Super::UploadParameterValue();
}

bool
UHoudiniAssetParameterChoice::SetParameterVariantValue( const FVariant & Variant, int32 Idx, bool bTriggerModify, bool bRecordUndo )
{
    int32 VariantType = Variant.GetType();
    int32 VariantValue = 0;

    switch ( VariantType )
    {
        case EVariantTypes::String:
        {
            if ( bStringChoiceList )
            {
                bool bLabelFound = false;
                const FString & VariantStringValue = Variant.GetValue< FString >();

                // We need to match selection based on label.
                int32 LabelIdx = 0;
                for ( ; LabelIdx < StringChoiceLabels.Num(); ++LabelIdx )
                {
                    FString * ChoiceLabel = StringChoiceLabels[ LabelIdx ].Get();

                    if ( ChoiceLabel->Equals( VariantStringValue ) )
                    {
                        VariantValue = LabelIdx;
                        bLabelFound = true;
                        break;
                    }
                }

                if ( !bLabelFound )
                    return false;
            }
            else
            {
                return false;
            }

            break;
        }

        case EVariantTypes::Int8:
        case EVariantTypes::Int16:
        case EVariantTypes::Int32:
        case EVariantTypes::Int64:
        case EVariantTypes::UInt8:
        case EVariantTypes::UInt16:
        case EVariantTypes::UInt32:
        case EVariantTypes::UInt64:
        {
            if ( CurrentValue >= 0 && CurrentValue < StringChoiceValues.Num() )
            {
                VariantValue = Variant.GetValue< int32 >();
            }
            else
            {
                return false;
            }

            break;
        }

        default:
        {
            return false;
        }
    }

#if WITH_EDITOR

    FScopedTransaction Transaction(
        TEXT( HOUDINI_MODULE_RUNTIME ),
        LOCTEXT( "HoudiniAssetParameterChoiceChange", "Houdini Parameter Choice: Changing a value" ),
        PrimaryObject );

    Modify();

    if ( !bRecordUndo )
        Transaction.Cancel();

#endif // WITH_EDITOR

    MarkPreChanged( bTriggerModify );
    CurrentValue = VariantValue;
    MarkChanged( bTriggerModify );

    return false;
}

void
UHoudiniAssetParameterChoice::Serialize( FArchive & Ar )
{
    // Call base implementation.
    Super::Serialize( Ar );

    Ar.UsingCustomVersion( FHoudiniCustomSerializationVersion::GUID );

    if ( Ar.IsLoading() )
    {
        StringChoiceValues.Empty();
        StringChoiceLabels.Empty();
    }

    int32 NumChoices = StringChoiceValues.Num();
    Ar << NumChoices;

    int32 NumLabels = StringChoiceLabels.Num();
    Ar << NumLabels;

    if ( Ar.IsSaving() )
    {
        for ( int32 ChoiceIdx = 0; ChoiceIdx < StringChoiceValues.Num(); ++ChoiceIdx )
            Ar << *( StringChoiceValues[ ChoiceIdx ].Get() );

        for ( int32 LabelIdx = 0; LabelIdx < StringChoiceLabels.Num(); ++LabelIdx )
            Ar << *( StringChoiceLabels[ LabelIdx ].Get() );
    }
    else if ( Ar.IsLoading() )
    {
        FString Temp;

        for ( int32 ChoiceIdx = 0; ChoiceIdx < NumChoices; ++ChoiceIdx )
        {
            Ar << Temp;
            FString * ChoiceName = new FString( Temp );
            StringChoiceValues.Add( TSharedPtr< FString >( ChoiceName ) );
        }

        for ( int32 LabelIdx = 0; LabelIdx < NumLabels; ++LabelIdx )
        {
            Ar << Temp;
            FString * ChoiceLabel = new FString( Temp );
            StringChoiceLabels.Add( TSharedPtr< FString >( ChoiceLabel ) );
        }
    }

    Ar << StringValue;
    Ar << CurrentValue;

    Ar << bStringChoiceList;
}

#if WITH_EDITOR

TSharedRef< SWidget >
UHoudiniAssetParameterChoice::CreateChoiceEntryWidget( TSharedPtr< FString > ChoiceEntry )
{
    FText ChoiceEntryText = FText::FromString( *ChoiceEntry );

    return SNew( STextBlock )
        .Text( ChoiceEntryText )
        .ToolTipText( ChoiceEntryText )
        .Font( FEditorStyle::GetFontStyle( TEXT( "PropertyWindow.NormalFont" ) ) );
}

void
UHoudiniAssetParameterChoice::OnChoiceChange( TSharedPtr< FString > NewChoice, ESelectInfo::Type SelectType )
{
    if ( !NewChoice.IsValid() )
        return;

    bool bLocalChanged = false;
    StringValue = *( NewChoice.Get() );

    // We need to match selection based on label.
    int32 LabelIdx = 0;
    for ( ; LabelIdx < StringChoiceLabels.Num(); ++LabelIdx )
    {
        FString * ChoiceLabel = StringChoiceLabels[ LabelIdx ].Get();

        if ( ChoiceLabel->Equals( StringValue ) )
        {
            bLocalChanged = true;
            break;
        }
    }

    if ( bLocalChanged )
    {
        // Record undo information.
        FScopedTransaction Transaction(
            TEXT( HOUDINI_MODULE_RUNTIME ),
            LOCTEXT( "HoudiniAssetParameterChoiceChange", "Houdini Parameter Choice: Changing a value" ),
            PrimaryObject );
        Modify();

        MarkPreChanged();

        CurrentValue = LabelIdx;

        // Mark this property as changed.
        MarkChanged();
    }
}

FText
UHoudiniAssetParameterChoice::HandleChoiceContentText() const
{
    return FText::FromString( StringValue );
}

#endif // WITH_EDITOR
