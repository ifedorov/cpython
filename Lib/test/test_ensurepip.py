import unittest
import unittest.mock
import ensurepip
import test.support
import os
import os.path
import contextlib
import sys


class TestEnsurePipVersion(unittest.TestCase):

    def test_returns_version(self):
        self.assertEqual(ensurepip._PIP_VERSION, ensurepip.version())


class TestBootstrap(unittest.TestCase):

    def setUp(self):
        run_pip_patch = unittest.mock.patch("ensurepip._run_pip")
        self.run_pip = run_pip_patch.start()
        self.addCleanup(run_pip_patch.stop)

        # Avoid side effects on the actual os module
        os_patch = unittest.mock.patch("ensurepip.os")
        patched_os = os_patch.start()
        self.addCleanup(os_patch.stop)
        patched_os.path = os.path
        self.os_environ = patched_os.environ = os.environ.copy()

    def test_basic_bootstrapping(self):
        ensurepip.bootstrap()

        self.run_pip.assert_called_once_with(
            [
                "install", "--no-index", "--find-links",
                unittest.mock.ANY, "--pre", "setuptools", "pip",
            ],
            unittest.mock.ANY,
        )

        additional_paths = self.run_pip.call_args[0][1]
        self.assertEqual(len(additional_paths), 2)

    def test_bootstrapping_with_root(self):
        ensurepip.bootstrap(root="/foo/bar/")

        self.run_pip.assert_called_once_with(
            [
                "install", "--no-index", "--find-links",
                unittest.mock.ANY, "--pre", "--root", "/foo/bar/",
                "setuptools", "pip",
            ],
            unittest.mock.ANY,
        )

    def test_bootstrapping_with_user(self):
        ensurepip.bootstrap(user=True)

        self.run_pip.assert_called_once_with(
            [
                "install", "--no-index", "--find-links",
                unittest.mock.ANY, "--pre", "--user", "setuptools", "pip",
            ],
            unittest.mock.ANY,
        )

    def test_bootstrapping_with_upgrade(self):
        ensurepip.bootstrap(upgrade=True)

        self.run_pip.assert_called_once_with(
            [
                "install", "--no-index", "--find-links",
                unittest.mock.ANY, "--pre", "--upgrade", "setuptools", "pip",
            ],
            unittest.mock.ANY,
        )

    def test_bootstrapping_with_verbosity_1(self):
        ensurepip.bootstrap(verbosity=1)

        self.run_pip.assert_called_once_with(
            [
                "install", "--no-index", "--find-links",
                unittest.mock.ANY, "--pre", "-v", "setuptools", "pip",
            ],
            unittest.mock.ANY,
        )

    def test_bootstrapping_with_verbosity_2(self):
        ensurepip.bootstrap(verbosity=2)

        self.run_pip.assert_called_once_with(
            [
                "install", "--no-index", "--find-links",
                unittest.mock.ANY, "--pre", "-vv", "setuptools", "pip",
            ],
            unittest.mock.ANY,
        )

    def test_bootstrapping_with_verbosity_3(self):
        ensurepip.bootstrap(verbosity=3)

        self.run_pip.assert_called_once_with(
            [
                "install", "--no-index", "--find-links",
                unittest.mock.ANY, "--pre", "-vvv", "setuptools", "pip",
            ],
            unittest.mock.ANY,
        )

    def test_bootstrapping_with_regular_install(self):
        ensurepip.bootstrap()
        self.assertEqual(self.os_environ["ENSUREPIP_OPTIONS"], "install")

    def test_bootstrapping_with_alt_install(self):
        ensurepip.bootstrap(altinstall=True)
        self.assertEqual(self.os_environ["ENSUREPIP_OPTIONS"], "altinstall")

    def test_bootstrapping_with_default_pip(self):
        ensurepip.bootstrap(default_pip=True)
        self.assertNotIn("ENSUREPIP_OPTIONS", self.os_environ)

    def test_altinstall_default_pip_conflict(self):
        with self.assertRaises(ValueError):
            ensurepip.bootstrap(altinstall=True, default_pip=True)
        self.run_pip.assert_not_called()

@contextlib.contextmanager
def fake_pip(version=ensurepip._PIP_VERSION):
    if version is None:
        pip = None
    else:
        class FakePip():
            __version__ = version
        pip = FakePip()
    sentinel = object()
    orig_pip = sys.modules.get("pip", sentinel)
    sys.modules["pip"] = pip
    try:
        yield pip
    finally:
        if orig_pip is sentinel:
            del sys.modules["pip"]
        else:
            sys.modules["pip"] = orig_pip

class TestUninstall(unittest.TestCase):

    def setUp(self):
        run_pip_patch = unittest.mock.patch("ensurepip._run_pip")
        self.run_pip = run_pip_patch.start()
        self.addCleanup(run_pip_patch.stop)

    def test_uninstall_skipped_when_not_installed(self):
        with fake_pip(None):
            ensurepip._uninstall()
        self.run_pip.assert_not_called()

    def test_uninstall_fails_with_wrong_version(self):
        with fake_pip("not a valid version"):
            with self.assertRaises(RuntimeError):
                ensurepip._uninstall()
        self.run_pip.assert_not_called()


    def test_uninstall(self):
        with fake_pip():
            ensurepip._uninstall()

        self.run_pip.assert_called_once_with(
            ["uninstall", "-y", "pip", "setuptools"]
        )

    def test_uninstall_with_verbosity_1(self):
        with fake_pip():
            ensurepip._uninstall(verbosity=1)

        self.run_pip.assert_called_once_with(
            ["uninstall", "-y", "-v", "pip", "setuptools"]
        )

    def test_uninstall_with_verbosity_2(self):
        with fake_pip():
            ensurepip._uninstall(verbosity=2)

        self.run_pip.assert_called_once_with(
            ["uninstall", "-y", "-vv", "pip", "setuptools"]
        )

    def test_uninstall_with_verbosity_3(self):
        with fake_pip():
            ensurepip._uninstall(verbosity=3)

        self.run_pip.assert_called_once_with(
            ["uninstall", "-y", "-vvv", "pip", "setuptools"]
        )




if __name__ == "__main__":
    test.support.run_unittest(__name__)
