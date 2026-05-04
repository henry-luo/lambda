import resolve: lambda.package.pdf.resolve
pn main() {
    let doc^err = input("test/input/invoice.pdf", 'pdf')
    let page = doc.pages[0]
    let xo = resolve.deref(doc, page.dict.Resources.XObject.I1)
    print({
      img_dict: xo.dictionary,
      img_data_len: len(xo.data),
      smask: resolve.deref(doc, xo.dictionary.SMask)
    })
}
