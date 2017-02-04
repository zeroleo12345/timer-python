from distutils.core import setup, Extension

setup(name             = "timer",
      version          = "0.1",
      description      = "High frequency start/stop timer",
      author           = "Brian Curtin",
      maintainer       = "Brian Curtin",
      license          = "PSF",
      maintainer_email = "brian@python.org",
      packages         = ["timer", "timer.tests"],
      ext_modules      = [Extension("timer._timer", ["src/_timer.c"])],
      classifiers      = [
        "Development Status :: 3 - Alpha",
        "Intended Audience :: Developers",
        "License :: OSI Approved :: Python Software Foundation License",
        "Operating System :: Microsoft",
        "Operating System :: Microsoft :: Windows",
        "Operating System :: Microsoft :: Windows :: Windows NT/2000",
        "Programming Language :: Python :: 2",
        "Programming Language :: Python :: 2.7",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.1",
        "Programming Language :: Python :: 3.2"],
      long_description = open("README.txt").read()
)
