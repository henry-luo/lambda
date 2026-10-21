"""Regression checks for PDF golden comparison, independent of native rendering."""
import unittest
import zlib

from pdf_output_check import compare_objects


def stream_object(identifier, dictionary, pixels):
    compressed = zlib.compress(pixels)
    return (f"{identifier} 0 obj\n<< {dictionary} /Filter /FlateDecode /Length {len(compressed)} >>\nstream\n".encode()
            + compressed + b"\nendstream\nendobj\n")


def image_objects(color=173, alpha=28):
    image = "/Type /XObject /Subtype /Image /Width 1 /Height 1 /ColorSpace /Device"
    return [stream_object(1, image + "Gray /BitsPerComponent 8", bytes([alpha])),
            stream_object(2, image + "RGB /BitsPerComponent 8 /SMask 1 0 R", bytes([color]) * 3),
            stream_object(3, "", b"BT\n(Exact text) Tj\nET\n")]


class PdfGoldenComparisonTest(unittest.TestCase):
    def test_native_rounding_compares_premultiplied_color_and_alpha(self):
        # these straight colors both reconstruct the native premultiplied value 19.
        compare_objects(image_objects(167, 29), image_objects())

    def test_larger_alpha_change_fails(self):
        with self.assertRaisesRegex(AssertionError, "object 1 differs"):
            compare_objects(image_objects(162, 30), image_objects())

    def test_larger_premultiplied_color_change_fails(self):
        with self.assertRaisesRegex(AssertionError, "object 2 differs"):
            compare_objects(image_objects(211), image_objects())

    def test_image_dictionary_changes_fail(self):
        for old, new in [(b"/Width 1", b"/Width 2"), (b"/SMask 1", b"/SMask 3"),
                         (b"/DeviceRGB", b"/DeviceCMYK"), (b"/BitsPerComponent 8", b"/BitsPerComponent 4")]:
            with self.subTest(change=new):
                actual = image_objects(167, 29)
                actual[1] = actual[1].replace(old, new)
                with self.assertRaises(AssertionError):
                    compare_objects(actual, image_objects())

    def test_nonimage_streams_still_compare_exactly(self):
        actual = image_objects()
        actual[2] = stream_object(3, "", b"BT\n(Changed text) Tj\nET\n")
        with self.assertRaisesRegex(AssertionError, "object 3 differs"):
            compare_objects(actual, image_objects())

    def test_unpaired_gray_image_still_compares_exactly(self):
        with self.assertRaisesRegex(AssertionError, "object 1 differs"):
            compare_objects(image_objects(alpha=29)[:1], image_objects()[:1])

    def test_truncated_image_fails(self):
        actual = image_objects()
        actual[1] = stream_object(2, "/Type /XObject /Subtype /Image /Width 1 /Height 1 "
                                  "/ColorSpace /DeviceRGB /BitsPerComponent 8 /SMask 1 0 R", b"\xad\xad")
        with self.assertRaisesRegex(AssertionError, "object 2 differs"):
            compare_objects(actual, image_objects())

    def test_object_count_still_compares_exactly(self):
        with self.assertRaisesRegex(AssertionError, "object count"):
            compare_objects(image_objects()[:2], image_objects())


if __name__ == "__main__":
    unittest.main()
