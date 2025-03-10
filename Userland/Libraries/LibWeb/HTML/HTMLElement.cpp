/*
 * Copyright (c) 2018-2022, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/StringBuilder.h>
#include <LibJS/Interpreter.h>
#include <LibWeb/ARIA/Roles.h>
#include <LibWeb/Bindings/ExceptionOrUtils.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/DOM/IDLEventListener.h>
#include <LibWeb/DOM/ShadowRoot.h>
#include <LibWeb/HTML/BrowsingContext.h>
#include <LibWeb/HTML/DOMStringMap.h>
#include <LibWeb/HTML/EventHandler.h>
#include <LibWeb/HTML/Focus.h>
#include <LibWeb/HTML/HTMLAnchorElement.h>
#include <LibWeb/HTML/HTMLAreaElement.h>
#include <LibWeb/HTML/HTMLBodyElement.h>
#include <LibWeb/HTML/HTMLElement.h>
#include <LibWeb/HTML/NavigableContainer.h>
#include <LibWeb/HTML/VisibilityState.h>
#include <LibWeb/HTML/Window.h>
#include <LibWeb/Layout/Box.h>
#include <LibWeb/Layout/BreakNode.h>
#include <LibWeb/Layout/TextNode.h>
#include <LibWeb/Painting/PaintableBox.h>
#include <LibWeb/UIEvents/EventNames.h>
#include <LibWeb/UIEvents/FocusEvent.h>
#include <LibWeb/UIEvents/MouseEvent.h>
#include <LibWeb/WebIDL/DOMException.h>
#include <LibWeb/WebIDL/ExceptionOr.h>

namespace Web::HTML {

HTMLElement::HTMLElement(DOM::Document& document, DOM::QualifiedName qualified_name)
    : Element(document, move(qualified_name))
{
}

HTMLElement::~HTMLElement() = default;

JS::ThrowCompletionOr<void> HTMLElement::initialize(JS::Realm& realm)
{
    MUST_OR_THROW_OOM(Base::initialize(realm));
    set_prototype(&Bindings::ensure_web_prototype<Bindings::HTMLElementPrototype>(realm, "HTMLElement"));

    m_dataset = TRY(Bindings::throw_dom_exception_if_needed(realm.vm(), [&]() {
        return DOMStringMap::create(*this);
    }));

    return {};
}

void HTMLElement::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_dataset.ptr());
}

// https://html.spec.whatwg.org/multipage/dom.html#dom-dir
DeprecatedString HTMLElement::dir() const
{
    auto dir = attribute(HTML::AttributeNames::dir);
#define __ENUMERATE_HTML_ELEMENT_DIR_ATTRIBUTE(keyword) \
    if (dir.equals_ignoring_ascii_case(#keyword##sv))   \
        return #keyword##sv;
    ENUMERATE_HTML_ELEMENT_DIR_ATTRIBUTES
#undef __ENUMERATE_HTML_ELEMENT_DIR_ATTRIBUTE

    return {};
}

void HTMLElement::set_dir(DeprecatedString const& dir)
{
    MUST(set_attribute(HTML::AttributeNames::dir, dir));
}

HTMLElement::ContentEditableState HTMLElement::content_editable_state() const
{
    auto contenteditable = attribute(HTML::AttributeNames::contenteditable);
    // "true", an empty string or a missing value map to the "true" state.
    if ((!contenteditable.is_null() && contenteditable.is_empty()) || contenteditable.equals_ignoring_ascii_case("true"sv))
        return ContentEditableState::True;
    // "false" maps to the "false" state.
    if (contenteditable.equals_ignoring_ascii_case("false"sv))
        return ContentEditableState::False;
    // Having no such attribute or an invalid value maps to the "inherit" state.
    return ContentEditableState::Inherit;
}

bool HTMLElement::is_editable() const
{
    switch (content_editable_state()) {
    case ContentEditableState::True:
        return true;
    case ContentEditableState::False:
        return false;
    case ContentEditableState::Inherit:
        return parent() && parent()->is_editable();
    default:
        VERIFY_NOT_REACHED();
    }
}

DeprecatedString HTMLElement::content_editable() const
{
    switch (content_editable_state()) {
    case ContentEditableState::True:
        return "true";
    case ContentEditableState::False:
        return "false";
    case ContentEditableState::Inherit:
        return "inherit";
    default:
        VERIFY_NOT_REACHED();
    }
}

// https://html.spec.whatwg.org/multipage/interaction.html#contenteditable
WebIDL::ExceptionOr<void> HTMLElement::set_content_editable(DeprecatedString const& content_editable)
{
    if (content_editable.equals_ignoring_ascii_case("inherit"sv)) {
        remove_attribute(HTML::AttributeNames::contenteditable);
        return {};
    }
    if (content_editable.equals_ignoring_ascii_case("true"sv)) {
        MUST(set_attribute(HTML::AttributeNames::contenteditable, "true"));
        return {};
    }
    if (content_editable.equals_ignoring_ascii_case("false"sv)) {
        MUST(set_attribute(HTML::AttributeNames::contenteditable, "false"));
        return {};
    }
    return WebIDL::SyntaxError::create(realm(), "Invalid contentEditable value, must be 'true', 'false', or 'inherit'");
}

void HTMLElement::set_inner_text(StringView text)
{
    remove_all_children();
    MUST(append_child(document().create_text_node(text)));

    set_needs_style_update(true);
}

DeprecatedString HTMLElement::inner_text()
{
    StringBuilder builder;

    // innerText for element being rendered takes visibility into account, so force a layout and then walk the layout tree.
    document().update_layout();
    if (!layout_node())
        return text_content();

    Function<void(Layout::Node const&)> recurse = [&](auto& node) {
        for (auto* child = node.first_child(); child; child = child->next_sibling()) {
            if (is<Layout::TextNode>(child))
                builder.append(verify_cast<Layout::TextNode>(*child).text_for_rendering());
            if (is<Layout::BreakNode>(child))
                builder.append('\n');
            recurse(*child);
        }
    };
    recurse(*layout_node());

    return builder.to_deprecated_string();
}

// // https://drafts.csswg.org/cssom-view/#dom-htmlelement-offsettop
int HTMLElement::offset_top() const
{
    // NOTE: Ensure that layout is up-to-date before looking at metrics.
    const_cast<DOM::Document&>(document()).update_layout();

    if (is<HTML::HTMLBodyElement>(this) || !layout_node() || !parent_element() || !parent_element()->layout_node())
        return 0;
    auto position = layout_node()->box_type_agnostic_position();
    auto parent_position = parent_element()->layout_node()->box_type_agnostic_position();
    return position.y().value() - parent_position.y().value();
}

// https://drafts.csswg.org/cssom-view/#dom-htmlelement-offsetleft
int HTMLElement::offset_left() const
{
    // NOTE: Ensure that layout is up-to-date before looking at metrics.
    const_cast<DOM::Document&>(document()).update_layout();

    if (is<HTML::HTMLBodyElement>(this) || !layout_node() || !parent_element() || !parent_element()->layout_node())
        return 0;
    auto position = layout_node()->box_type_agnostic_position();
    auto parent_position = parent_element()->layout_node()->box_type_agnostic_position();
    return position.x().value() - parent_position.x().value();
}

// https://drafts.csswg.org/cssom-view/#dom-htmlelement-offsetwidth
int HTMLElement::offset_width() const
{
    // NOTE: Ensure that layout is up-to-date before looking at metrics.
    const_cast<DOM::Document&>(document()).update_layout();

    // 1. If the element does not have any associated CSS layout box return zero and terminate this algorithm.
    if (!paint_box())
        return 0;

    // 2. Return the width of the axis-aligned bounding box of the border boxes of all fragments generated by the element’s principal box,
    //    ignoring any transforms that apply to the element and its ancestors.
    // FIXME: Account for inline boxes.
    return paint_box()->border_box_width().value();
}

// https://drafts.csswg.org/cssom-view/#dom-htmlelement-offsetheight
int HTMLElement::offset_height() const
{
    // NOTE: Ensure that layout is up-to-date before looking at metrics.
    const_cast<DOM::Document&>(document()).update_layout();

    // 1. If the element does not have any associated CSS layout box return zero and terminate this algorithm.
    if (!paint_box())
        return 0;

    // 2. Return the height of the axis-aligned bounding box of the border boxes of all fragments generated by the element’s principal box,
    //    ignoring any transforms that apply to the element and its ancestors.
    // FIXME: Account for inline boxes.
    return paint_box()->border_box_height().value();
}

// https://html.spec.whatwg.org/multipage/links.html#cannot-navigate
bool HTMLElement::cannot_navigate() const
{
    // An element element cannot navigate if one of the following is true:

    // - element's node document is not fully active
    if (!document().is_fully_active())
        return true;

    // - element is not an a element and is not connected.
    return !is<HTML::HTMLAnchorElement>(this) && !is_connected();
}

void HTMLElement::parse_attribute(DeprecatedFlyString const& name, DeprecatedString const& value)
{
    Element::parse_attribute(name, value);

    // 1. If namespace is not null, or localName is not the name of an event handler content attribute on element, then return.
    // FIXME: Add the namespace part once we support attribute namespaces.
#undef __ENUMERATE
#define __ENUMERATE(attribute_name, event_name)                                                                     \
    if (name == HTML::AttributeNames::attribute_name) {                                                             \
        element_event_handler_attribute_changed(event_name, String::from_deprecated_string(value).release_value()); \
    }
    ENUMERATE_GLOBAL_EVENT_HANDLERS(__ENUMERATE)
#undef __ENUMERATE
}

// https://html.spec.whatwg.org/multipage/interaction.html#dom-focus
void HTMLElement::focus()
{
    // 1. If the element is marked as locked for focus, then return.
    if (m_locked_for_focus)
        return;

    // 2. Mark the element as locked for focus.
    m_locked_for_focus = true;

    // 3. Run the focusing steps for the element.
    run_focusing_steps(this);

    // FIXME: 4. If the value of the preventScroll dictionary member of options is false,
    //           then scroll the element into view with scroll behavior "auto",
    //           block flow direction position set to an implementation-defined value,
    //           and inline base direction position set to an implementation-defined value.

    // 5. Unmark the element as locked for focus.
    m_locked_for_focus = false;
}

// https://html.spec.whatwg.org/multipage/webappapis.html#fire-a-synthetic-pointer-event
bool HTMLElement::fire_a_synthetic_pointer_event(FlyString const& type, DOM::Element& target, bool not_trusted)
{
    // 1. Let event be the result of creating an event using PointerEvent.
    // 2. Initialize event's type attribute to e.
    // FIXME: Actually create a PointerEvent!
    auto event = UIEvents::MouseEvent::create(realm(), type).release_value_but_fixme_should_propagate_errors();

    // 3. Initialize event's bubbles and cancelable attributes to true.
    event->set_bubbles(true);
    event->set_cancelable(true);

    // 4. Set event's composed flag.
    event->set_composed(true);

    // 5. If the not trusted flag is set, initialize event's isTrusted attribute to false.
    if (not_trusted) {
        event->set_is_trusted(false);
    }

    // FIXME: 6. Initialize event's ctrlKey, shiftKey, altKey, and metaKey attributes according to the current state
    //           of the key input device, if any (false for any keys that are not available).

    // FIXME: 7. Initialize event's view attribute to target's node document's Window object, if any, and null otherwise.

    // FIXME: 8. event's getModifierState() method is to return values appropriately describing the current state of the key input device.

    // 9. Return the result of dispatching event at target.
    return target.dispatch_event(event);
}

// https://html.spec.whatwg.org/multipage/interaction.html#dom-click
void HTMLElement::click()
{
    // FIXME: 1. If this element is a form control that is disabled, then return.

    // 2. If this element's click in progress flag is set, then return.
    if (m_click_in_progress)
        return;

    // 3. Set this element's click in progress flag.
    m_click_in_progress = true;

    // FIXME: 4. Fire a synthetic pointer event named click at this element, with the not trusted flag set.
    fire_a_synthetic_pointer_event(HTML::EventNames::click, *this, true);

    // 5. Unset this element's click in progress flag.
    m_click_in_progress = false;
}

// https://html.spec.whatwg.org/multipage/interaction.html#dom-blur
void HTMLElement::blur()
{
    // The blur() method, when invoked, should run the unfocusing steps for the element on which the method was called.
    run_unfocusing_steps(this);

    // User agents may selectively or uniformly ignore calls to this method for usability reasons.
}

Optional<ARIA::Role> HTMLElement::default_role() const
{
    // https://www.w3.org/TR/html-aria/#el-article
    if (local_name() == TagNames::article)
        return ARIA::Role::article;
    // https://www.w3.org/TR/html-aria/#el-aside
    if (local_name() == TagNames::aside)
        return ARIA::Role::complementary;
    // https://www.w3.org/TR/html-aria/#el-b
    if (local_name() == TagNames::b)
        return ARIA::Role::generic;
    // https://www.w3.org/TR/html-aria/#el-bdi
    if (local_name() == TagNames::bdi)
        return ARIA::Role::generic;
    // https://www.w3.org/TR/html-aria/#el-bdo
    if (local_name() == TagNames::bdo)
        return ARIA::Role::generic;
    // https://www.w3.org/TR/html-aria/#el-code
    if (local_name() == TagNames::code)
        return ARIA::Role::code;
    // https://www.w3.org/TR/html-aria/#el-dfn
    if (local_name() == TagNames::dfn)
        return ARIA::Role::term;
    // https://www.w3.org/TR/html-aria/#el-em
    if (local_name() == TagNames::em)
        return ARIA::Role::emphasis;
    // https://www.w3.org/TR/html-aria/#el-figure
    if (local_name() == TagNames::figure)
        return ARIA::Role::figure;
    // https://www.w3.org/TR/html-aria/#el-footer
    if (local_name() == TagNames::footer) {
        // TODO: If not a descendant of an article, aside, main, nav or section element, or an element with role=article, complementary, main, navigation or region then role=contentinfo
        // Otherwise, role=generic
        return ARIA::Role::generic;
    }
    // https://www.w3.org/TR/html-aria/#el-header
    if (local_name() == TagNames::header) {
        // TODO: If not a descendant of an article, aside, main, nav or section element, or an element with role=article, complementary, main, navigation or region then role=banner
        // Otherwise, role=generic
        return ARIA::Role::generic;
    }
    // https://www.w3.org/TR/html-aria/#el-hgroup
    if (local_name() == TagNames::hgroup)
        return ARIA::Role::generic;
    // https://www.w3.org/TR/html-aria/#el-i
    if (local_name() == TagNames::i)
        return ARIA::Role::generic;
    // https://www.w3.org/TR/html-aria/#el-main
    if (local_name() == TagNames::main)
        return ARIA::Role::main;
    // https://www.w3.org/TR/html-aria/#el-nav
    if (local_name() == TagNames::nav)
        return ARIA::Role::navigation;
    // https://www.w3.org/TR/html-aria/#el-samp
    if (local_name() == TagNames::samp)
        return ARIA::Role::generic;
    // https://www.w3.org/TR/html-aria/#el-section
    if (local_name() == TagNames::section) {
        // TODO:  role=region if the section element has an accessible name
        //        Otherwise, no corresponding role
        return ARIA::Role::region;
    }
    // https://www.w3.org/TR/html-aria/#el-small
    if (local_name() == TagNames::small)
        return ARIA::Role::generic;
    // https://www.w3.org/TR/html-aria/#el-strong
    if (local_name() == TagNames::strong)
        return ARIA::Role::strong;
    // https://www.w3.org/TR/html-aria/#el-sub
    if (local_name() == TagNames::sub)
        return ARIA::Role::subscript;
    // https://www.w3.org/TR/html-aria/#el-summary
    if (local_name() == TagNames::summary)
        return ARIA::Role::button;
    // https://www.w3.org/TR/html-aria/#el-sup
    if (local_name() == TagNames::sup)
        return ARIA::Role::superscript;
    // https://www.w3.org/TR/html-aria/#el-u
    if (local_name() == TagNames::u)
        return ARIA::Role::generic;

    return {};
}

}
