:mod:`timer` -- a simple timer using platform threads
=====================================================

:Author: Brian Curtin <curtin@acm.org>
:Date: |today|
:Summary: C extension to replace the stdlib's threading.Timer by using platform
          threads.


.. class:: Timer(duration, callback, *args, **kwargs)

   Create a :class:`Timer` object given a `duration` in microseconds,
   and a `callback` callable object. The `args` and `kwargs` will be
   given to the `callback`.

   .. method:: start()

      Start a :class:`Timer` thread using the platform's threads.
      `CreateThread <http://msdn.microsoft.com/en-us/library/ms682453(VS.85).aspx>`__
      is used on Windows, and
      `pthread_create <http://www.opengroup.org/onlinepubs/009695399/functions/pthread_create.html>`__
      is used on Mac and Linux platforms. Due to this, the thread
      runs outside of CPython's GIL.
   
   .. method:: stop()
   
      Stop a :class:`Timer` thread and return the current elapsed time
      in microseconds.
   
   .. method:: reset()
   
      Cleanup a :class:`Timer` object. Calling this method resets the
      :data:`running` and :data:`expired` members to `False`, and
      resets the :data:`elapsed` member to 0.
      
      If you plan to reuse a :class:`Timer`, it is suggested that you
      call this method before calling :meth:`start` again.
   
   .. data:: elapsed
   
      Return the elapsed time of the :class:`Timer` object in its
      stopped state. The same value is returned when calling
      :meth:`stop`.
      
      This value is not constantly updated with the current elapsed
      time. The value remains 0 until the :class:`Timer` has been
      stopped or is expired.
   
   .. data:: expired
   
      Set to `True` if the :class:`Timer` thread was allowed to run
      until the configured `duration`.
   
   .. data:: running
   
      Set to `True` as long as the :class:`Timer` thread hasn't
      expired or been stopped.
