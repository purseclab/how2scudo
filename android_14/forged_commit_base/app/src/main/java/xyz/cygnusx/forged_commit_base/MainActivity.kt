package xyz.cygnusx.forged_commit_base

import androidx.appcompat.app.AppCompatActivity
import android.os.Bundle
import android.util.Log
import android.widget.TextView
import xyz.cygnusx.forged_commit_base.databinding.ActivityMainBinding

class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        run()
    }

    /**
     * A native method that is implemented by the 'forged_commit_base' native library,
     * which is packaged with this application.
     */
    external fun run()

    companion object {
        private const val TAG = "FORGED_COMMIT_BASE"
        // Used to load the 'forged_commit_base' library on application startup.
        init {
            System.loadLibrary("forged_commit_base")
        }
    }
}