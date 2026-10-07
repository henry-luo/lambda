// ./lambda.exe view test/demo/slides/slide_northstar.ls
import slide: lambda.slide
import northstar: .northstar_deck

slide.page(northstar.deck, {instance: "northstar", autostart: true})^
