// Schema for element types - demonstrates element_type syntax
type HeaderElement = <header level: int, text: string>
type ParagraphElement = <p class: string?, content: string>
type LinkElement = <a href: string, target: string?, text: string>
type ImageElement = <img src: string, alt: string, width: int?, height: int?>

type ElementTypes = {
    header: HeaderElement,
    paragraph: ParagraphElement,
    link: LinkElement,
    image: ImageElement
}
